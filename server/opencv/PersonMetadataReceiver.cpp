#include "PersonMetadataReceiver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "AppConfig.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace cv;
using namespace std;

namespace
{
    using Clock = chrono::steady_clock;

    struct CoordinateTransform
    {
        bool present = false;
        double translateX = 0.0;
        double translateY = 0.0;
        double scaleX = 1.0;
        double scaleY = 1.0;
    };

    struct RawPersonBox
    {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        double confidence = 1.0;
        string objectId;
        CoordinateTransform transform;
    };

    string lowerCopy(string value)
    {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
            {
                return static_cast<char>(tolower(ch));
            });
        return value;
    }

    string trimCopy(const string& value)
    {
        size_t begin = 0;
        while (begin < value.size() &&
            isspace(static_cast<unsigned char>(value[begin]))) ++begin;

        size_t end = value.size();
        while (end > begin &&
            isspace(static_cast<unsigned char>(value[end - 1]))) --end;
        return value.substr(begin, end - begin);
    }

    bool localNameEquals(const string& qualifiedName, const string& expected)
    {
        const size_t colon = qualifiedName.rfind(':');
        const string local = colon == string::npos
            ? qualifiedName
            : qualifiedName.substr(colon + 1);
        return lowerCopy(local) == lowerCopy(expected);
    }

    size_t findOpeningTag(
        const string& text,
        const string& localName,
        size_t from,
        size_t* tagEnd = nullptr)
    {
        size_t position = from;
        while ((position = text.find('<', position)) != string::npos)
        {
            if (position + 1 >= text.size()) return string::npos;
            const char first = text[position + 1];
            if (first == '/' || first == '!' || first == '?')
            {
                ++position;
                continue;
            }

            const size_t nameBegin = position + 1;
            size_t nameEnd = nameBegin;
            while (nameEnd < text.size())
            {
                const char ch = text[nameEnd];
                if (isspace(static_cast<unsigned char>(ch)) || ch == '>' || ch == '/')
                    break;
                ++nameEnd;
            }

            if (nameEnd > nameBegin &&
                localNameEquals(text.substr(nameBegin, nameEnd - nameBegin), localName))
            {
                const size_t end = text.find('>', nameEnd);
                if (end == string::npos) return string::npos;
                if (tagEnd) *tagEnd = end;
                return position;
            }
            ++position;
        }
        return string::npos;
    }

    size_t findClosingTag(
        const string& text,
        const string& localName,
        size_t from,
        size_t* tagEnd = nullptr)
    {
        size_t position = from;
        while ((position = text.find("</", position)) != string::npos)
        {
            const size_t nameBegin = position + 2;
            size_t nameEnd = nameBegin;
            while (nameEnd < text.size())
            {
                const char ch = text[nameEnd];
                if (isspace(static_cast<unsigned char>(ch)) || ch == '>') break;
                ++nameEnd;
            }

            if (nameEnd > nameBegin &&
                localNameEquals(text.substr(nameBegin, nameEnd - nameBegin), localName))
            {
                const size_t end = text.find('>', nameEnd);
                if (end == string::npos) return string::npos;
                if (tagEnd) *tagEnd = end;
                return position;
            }
            position += 2;
        }
        return string::npos;
    }

    string tagText(const string& text, size_t begin, size_t end)
    {
        if (begin == string::npos || end == string::npos || end < begin)
            return string();
        return text.substr(begin, end - begin + 1);
    }

    bool extractAttribute(
        const string& tag,
        const string& attributeName,
        string& value)
    {
        const string lowerTag = lowerCopy(tag);
        const string lowerName = lowerCopy(attributeName);
        size_t position = 0;

        while ((position = lowerTag.find(lowerName, position)) != string::npos)
        {
            const bool leftBoundary = position == 0 ||
                !(isalnum(static_cast<unsigned char>(lowerTag[position - 1])) ||
                    lowerTag[position - 1] == '_' || lowerTag[position - 1] == '-');
            const size_t afterName = position + lowerName.size();
            const bool rightBoundary = afterName >= lowerTag.size() ||
                !(isalnum(static_cast<unsigned char>(lowerTag[afterName])) ||
                    lowerTag[afterName] == '_' || lowerTag[afterName] == '-');
            if (!leftBoundary || !rightBoundary)
            {
                position = afterName;
                continue;
            }

            size_t equals = afterName;
            while (equals < tag.size() &&
                isspace(static_cast<unsigned char>(tag[equals]))) ++equals;
            if (equals >= tag.size() || tag[equals] != '=')
            {
                position = afterName;
                continue;
            }

            size_t quotePosition = equals + 1;
            while (quotePosition < tag.size() &&
                isspace(static_cast<unsigned char>(tag[quotePosition]))) ++quotePosition;
            if (quotePosition >= tag.size() ||
                (tag[quotePosition] != '"' && tag[quotePosition] != '\''))
                return false;

            const char quote = tag[quotePosition];
            const size_t valueEnd = tag.find(quote, quotePosition + 1);
            if (valueEnd == string::npos) return false;
            value = tag.substr(quotePosition + 1, valueEnd - quotePosition - 1);
            return true;
        }
        return false;
    }

    bool extractNumber(const string& tag, const string& name, double& value)
    {
        string number;
        if (!extractAttribute(tag, name, number)) return false;
        char* end = nullptr;
        const double parsed = strtod(number.c_str(), &end);
        if (!end || end == number.c_str() || !isfinite(parsed)) return false;
        value = parsed;
        return true;
    }

    bool isPersonClass(string value)
    {
        value = lowerCopy(trimCopy(value));
        const size_t colon = value.rfind(':');
        if (colon != string::npos) value = value.substr(colon + 1);
        value.erase(remove_if(value.begin(), value.end(), [](unsigned char ch)
            {
                return ch == ' ' || ch == '_' || ch == '-';
            }), value.end());
        return value == "person" || value == "human" ||
            value == "humanbody" || value == "body";
    }

    bool containsPersonClass(const string& objectBlock, double& confidence)
    {
        size_t position = 0;
        while (true)
        {
            size_t openEnd = string::npos;
            const size_t open = findOpeningTag(objectBlock, "Type", position, &openEnd);
            if (open == string::npos) break;
            size_t closeEnd = string::npos;
            const size_t close = findClosingTag(objectBlock, "Type", openEnd + 1, &closeEnd);
            if (close == string::npos) break;

            if (isPersonClass(objectBlock.substr(openEnd + 1, close - openEnd - 1)))
            {
                confidence = 1.0;
                const bool likelihoodAttribute = extractNumber(
                    tagText(objectBlock, open, openEnd),
                    "Likelihood", confidence);
                if (!likelihoodAttribute)
                {
                    // Older ONVIF schemas store likelihood as the next sibling
                    // element inside ClassCandidate instead of an attribute.
                    size_t likelihoodEnd = string::npos;
                    const size_t likelihoodOpen = findOpeningTag(
                        objectBlock, "Likelihood", closeEnd + 1, &likelihoodEnd);
                    if (likelihoodOpen != string::npos)
                    {
                        size_t likelihoodCloseEnd = string::npos;
                        const size_t likelihoodClose = findClosingTag(
                            objectBlock, "Likelihood", likelihoodEnd + 1,
                            &likelihoodCloseEnd);
                        if (likelihoodClose != string::npos)
                        {
                            const string value = trimCopy(objectBlock.substr(
                                likelihoodEnd + 1,
                                likelihoodClose - likelihoodEnd - 1));
                            char* end = nullptr;
                            const double parsed = strtod(value.c_str(), &end);
                            if (end && end != value.c_str() && isfinite(parsed))
                                confidence = parsed;
                        }
                    }
                }
                return true;
            }
            position = closeEnd + 1;
        }

        // Hanwha firmware variants may encode class and score as SimpleItem.
        position = 0;
        while (true)
        {
            size_t itemEnd = string::npos;
            const size_t item = findOpeningTag(
                objectBlock, "SimpleItem", position, &itemEnd);
            if (item == string::npos) break;

            const string itemTag = tagText(objectBlock, item, itemEnd);
            string name;
            string value;
            extractAttribute(itemTag, "Name", name);
            extractAttribute(itemTag, "Value", value);
            const string lowerName = lowerCopy(name);

            if ((lowerName.find("class") != string::npos ||
                lowerName.find("type") != string::npos ||
                lowerName.find("object") != string::npos) &&
                isPersonClass(value))
            {
                confidence = 1.0;
                return true;
            }
            position = itemEnd + 1;
        }
        return false;
    }

    CoordinateTransform parseTransform(const string& frameBlock)
    {
        CoordinateTransform output;
        size_t transformEnd = string::npos;
        const size_t transformStart = findOpeningTag(
            frameBlock, "Transformation", 0, &transformEnd);
        if (transformStart == string::npos) return output;

        size_t closeEnd = string::npos;
        const size_t transformClose = findClosingTag(
            frameBlock, "Transformation", transformEnd + 1, &closeEnd);
        const size_t blockEnd = transformClose == string::npos
            ? min(frameBlock.size(), transformEnd + static_cast<size_t>(1024))
            : transformClose;
        const string block = frameBlock.substr(
            transformStart, blockEnd - transformStart);

        size_t tagEnd = string::npos;
        const size_t translate = findOpeningTag(block, "Translate", 0, &tagEnd);
        if (translate != string::npos)
        {
            const string tag = tagText(block, translate, tagEnd);
            output.present |= extractNumber(tag, "x", output.translateX);
            output.present |= extractNumber(tag, "y", output.translateY);
        }

        tagEnd = string::npos;
        const size_t scale = findOpeningTag(block, "Scale", 0, &tagEnd);
        if (scale != string::npos)
        {
            const string tag = tagText(block, scale, tagEnd);
            output.present |= extractNumber(tag, "x", output.scaleX);
            output.present |= extractNumber(tag, "y", output.scaleY);
        }
        return output;
    }

    vector<RawPersonBox> parseFrame(const string& frameBlock)
    {
        vector<RawPersonBox> output;
        const CoordinateTransform transformValue = parseTransform(frameBlock);
        size_t position = 0;

        while (true)
        {
            size_t objectEnd = string::npos;
            const size_t objectStart = findOpeningTag(
                frameBlock, "Object", position, &objectEnd);
            if (objectStart == string::npos) break;

            size_t closeEnd = string::npos;
            const size_t objectClose = findClosingTag(
                frameBlock, "Object", objectEnd + 1, &closeEnd);
            if (objectClose == string::npos) break;
            const string block = frameBlock.substr(
                objectStart, closeEnd - objectStart + 1);

            double confidence = 1.0;
            if (!containsPersonClass(block, confidence))
            {
                position = closeEnd + 1;
                continue;
            }

            size_t boxEnd = string::npos;
            const size_t boxStart = findOpeningTag(block, "BoundingBox", 0, &boxEnd);
            if (boxStart == string::npos)
            {
                position = closeEnd + 1;
                continue;
            }

            RawPersonBox person;
            const string boxTag = tagText(block, boxStart, boxEnd);
            if (!extractNumber(boxTag, "left", person.left) ||
                !extractNumber(boxTag, "top", person.top) ||
                !extractNumber(boxTag, "right", person.right) ||
                !extractNumber(boxTag, "bottom", person.bottom))
            {
                position = closeEnd + 1;
                continue;
            }

            person.confidence = confidence;
            person.transform = transformValue;
            extractAttribute(tagText(frameBlock, objectStart, objectEnd),
                "ObjectId", person.objectId);
            output.push_back(std::move(person));
            position = closeEnd + 1;
        }
        return output;
    }

    Rect toPixelBox(const RawPersonBox& raw, const Size& size)
    {
        if (size.width <= 0 || size.height <= 0) return Rect();

        double left = raw.left;
        double top = raw.top;
        double right = raw.right;
        double bottom = raw.bottom;
        bool onvifCoordinates = false;
        bool imageNormalized = false;

        if (raw.transform.present)
        {
            left = left * raw.transform.scaleX + raw.transform.translateX;
            right = right * raw.transform.scaleX + raw.transform.translateX;
            top = top * raw.transform.scaleY + raw.transform.translateY;
            bottom = bottom * raw.transform.scaleY + raw.transform.translateY;
            onvifCoordinates = true;
        }
        else
        {
            const double minimum = min(min(left, right), min(top, bottom));
            const double maximum = max(max(left, right), max(top, bottom));
            if (minimum >= -1.25 && maximum <= 1.25)
            {
                imageNormalized =
                    minimum >= -0.001 && maximum <= 1.001 && top < bottom;
                onvifCoordinates = !imageNormalized;
            }
        }

        double x1;
        double y1;
        double x2;
        double y2;
        if (onvifCoordinates)
        {
            x1 = (min(left, right) + 1.0) * 0.5 * size.width;
            x2 = (max(left, right) + 1.0) * 0.5 * size.width;
            y1 = (1.0 - max(top, bottom)) * 0.5 * size.height;
            y2 = (1.0 - min(top, bottom)) * 0.5 * size.height;
        }
        else if (imageNormalized)
        {
            x1 = min(left, right) * size.width;
            x2 = max(left, right) * size.width;
            y1 = min(top, bottom) * size.height;
            y2 = max(top, bottom) * size.height;
        }
        else
        {
            x1 = min(left, right);
            x2 = max(left, right);
            y1 = min(top, bottom);
            y2 = max(top, bottom);
        }

        Rect box(
            cvFloor(x1),
            cvFloor(y1),
            max(1, cvCeil(x2) - cvFloor(x1)),
            max(1, cvCeil(y2) - cvFloor(y1)));
        const int padX = max(1, cvRound(
            box.width * person_metadata_config::BOX_PADDING_X_RATIO));
        const int padTop = max(1, cvRound(
            box.height * person_metadata_config::BOX_PADDING_TOP_RATIO));
        const int padBottom = max(1, cvRound(
            box.height * person_metadata_config::BOX_PADDING_BOTTOM_RATIO));
        box = Rect(
            box.x - padX,
            box.y - padTop,
            box.width + padX * 2,
            box.height + padTop + padBottom) &
            Rect(0, 0, size.width, size.height);
        return box;
    }

    vector<string> extractCompleteFrames(string& buffer)
    {
        vector<string> output;
        while (true)
        {
            size_t openEnd = string::npos;
            const size_t open = findOpeningTag(buffer, "Frame", 0, &openEnd);
            if (open == string::npos)
            {
                if (buffer.size() > person_metadata_config::BUFFER_LIMIT_BYTES)
                    buffer.erase(0, buffer.size() -
                        person_metadata_config::BUFFER_KEEP_BYTES);
                break;
            }

            size_t closeEnd = string::npos;
            const size_t close = findClosingTag(
                buffer, "Frame", openEnd + 1, &closeEnd);
            if (close == string::npos)
            {
                if (open > 0) buffer.erase(0, open);
                break;
            }

            output.push_back(buffer.substr(open, closeEnd - open + 1));
            buffer.erase(0, closeEnd + 1);
        }
        return output;
    }

    vector<string> ffmpegArguments(const string& rtspUrl)
    {
        return {
            person_metadata_config::FFMPEG_EXECUTABLE,
            "-nostdin",
            "-hide_banner",
            "-loglevel", "error",
            "-rtsp_transport",
            person_metadata_config::RTSP_USE_TCP ? "tcp" : "udp",
            "-timeout", to_string(person_metadata_config::SOCKET_TIMEOUT_US),
            "-allowed_media_types", "data",
            "-fflags", "nobuffer",
            "-i", rtspUrl,
            "-copy_unknown",
            "-map", person_metadata_config::STREAM_MAP,
            "-codec", "copy",
            "-f", "data",
            "pipe:1"
        };
    }

#ifdef _WIN32
    string quoteWindowsArgument(const string& argument)
    {
        if (argument.find_first_of(" \t\"") == string::npos) return argument;
        string quoted = "\"";
        size_t backslashes = 0;
        for (char ch : argument)
        {
            if (ch == '\\')
            {
                ++backslashes;
                continue;
            }
            if (ch == '"')
            {
                quoted.append(backslashes * 2 + 1, '\\');
                quoted.push_back('"');
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(ch);
        }
        quoted.append(backslashes * 2, '\\');
        quoted.push_back('"');
        return quoted;
    }

    string buildWindowsCommandLine(const vector<string>& arguments)
    {
        ostringstream command;
        for (size_t index = 0; index < arguments.size(); ++index)
        {
            if (index) command << ' ';
            command << quoteWindowsArgument(arguments[index]);
        }
        return command.str();
    }
#endif
}

class PersonMetadataReceiver::Impl
{
public:
    ~Impl()
    {
        stop();
    }

    bool start(const string& rtspUrl)
    {
        if (rtspUrl.empty() || !person_metadata_config::ENABLED) return false;
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return false;

        rtspUrl_ = rtspUrl;
        streamConnected_ = false;
        bytesReceived_ = 0;
        setStatus("waiting for WiseAI metadata");
        worker_ = thread(&Impl::workerLoop, this);
        return true;
    }

    void stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;

#ifdef _WIN32
        {
            lock_guard<mutex> lock(processMutex_);
            if (processHandle_) TerminateProcess(processHandle_, 0);
        }
#else
        {
            lock_guard<mutex> lock(processMutex_);
            if (processId_ > 0) kill(processId_, SIGTERM);
        }
#endif
        if (worker_.joinable()) worker_.join();
        streamConnected_ = false;
        setStatus("stopped");
    }

    PersonMetadataFrame snapshot(const Size& frameSize) const
    {
        PersonMetadataFrame output;
        output.receiverRunning = running_.load();
        output.streamConnected = streamConnected_.load();
        output.bytesReceived = bytesReceived_.load();

        vector<RawPersonBox> rawPersons;
        Clock::time_point updateTime;
        bool hasFrame = false;
        {
            lock_guard<mutex> lock(dataMutex_);
            rawPersons = latestPersons_;
            updateTime = lastFrameTime_;
            hasFrame = hasFrame_;
            output.status = status_;
        }
        if (!hasFrame) return output;

        output.ageMs =
            chrono::duration<double, milli>(Clock::now() - updateTime).count();
        if (output.ageMs > person_metadata_config::FRESH_MS) return output;

        for (const RawPersonBox& raw : rawPersons)
        {
            if (raw.confidence < person_metadata_config::MIN_CONFIDENCE) continue;
            const Rect box = toPixelBox(raw, frameSize);
            if (box.empty()) continue;

            PersonDetection person;
            person.box = box;
            person.confidence = raw.confidence;
            person.objectId = raw.objectId;
            output.persons.push_back(std::move(person));
        }
        return output;
    }

private:
    void setStatus(const string& status)
    {
        lock_guard<mutex> lock(dataMutex_);
        status_ = status;
    }

    void publish(vector<RawPersonBox> persons)
    {
        lock_guard<mutex> lock(dataMutex_);
        latestPersons_ = std::move(persons);
        lastFrameTime_ = Clock::now();
        hasFrame_ = true;
        status_ = "receiving WiseAI metadata";
    }

    void processBytes(const char* data, size_t size)
    {
        if (!data || size == 0) return;
        bytesReceived_.fetch_add(static_cast<uint64_t>(size));
        streamConnected_ = true;
        xmlBuffer_.append(data, size);
        for (const string& frame : extractCompleteFrames(xmlBuffer_))
            publish(parseFrame(frame));
    }

    void workerLoop()
    {
        while (running_.load())
        {
            streamConnected_ = false;
            xmlBuffer_.clear();
            setStatus("connecting to WiseAI metadata");
#ifdef _WIN32
            runWindowsOnce();
#else
            runPosixOnce();
#endif
            streamConnected_ = false;
            if (running_.load())
                this_thread::sleep_for(
                    chrono::milliseconds(person_metadata_config::RECONNECT_MS));
        }
    }

#ifdef _WIN32
    void runWindowsOnce()
    {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE outputRead = nullptr;
        HANDLE outputWrite = nullptr;
        if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
        {
            setStatus("cannot create FFmpeg output pipe");
            return;
        }
        SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

        HANDLE nullOutput = CreateFileA(
            "NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = outputWrite;
        startup.hStdError = nullOutput;

        PROCESS_INFORMATION process{};
        string commandLine = buildWindowsCommandLine(ffmpegArguments(rtspUrl_));
        const BOOL created = CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process);

        CloseHandle(outputWrite);
        if (nullOutput != INVALID_HANDLE_VALUE) CloseHandle(nullOutput);
        if (!created)
        {
            CloseHandle(outputRead);
            setStatus("FFmpeg was not found; add it to PATH");
            return;
        }

        {
            lock_guard<mutex> lock(processMutex_);
            processHandle_ = process.hProcess;
        }
        CloseHandle(process.hThread);

        char buffer[8192];
        while (running_.load())
        {
            DWORD count = 0;
            if (!ReadFile(outputRead, buffer, sizeof(buffer), &count, nullptr) ||
                count == 0)
                break;
            processBytes(buffer, static_cast<size_t>(count));
        }

        if (!running_.load()) TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 1000);
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(outputRead);

        {
            lock_guard<mutex> lock(processMutex_);
            if (processHandle_ == process.hProcess) processHandle_ = nullptr;
        }
        CloseHandle(process.hProcess);
        if (running_.load())
            setStatus("metadata track unavailable (FFmpeg exit " +
                to_string(exitCode) + ")");
    }
#else
    void runPosixOnce()
    {
        int outputPipe[2];
        if (pipe(outputPipe) != 0)
        {
            setStatus("cannot create FFmpeg output pipe");
            return;
        }

        const vector<string> arguments = ffmpegArguments(rtspUrl_);
        const pid_t child = fork();
        if (child == 0)
        {
            dup2(outputPipe[1], STDOUT_FILENO);
            const int nullFd = open("/dev/null", O_WRONLY);
            if (nullFd >= 0)
            {
                dup2(nullFd, STDERR_FILENO);
                close(nullFd);
            }
            close(outputPipe[0]);
            close(outputPipe[1]);

            vector<char*> argv;
            argv.reserve(arguments.size() + 1);
            for (const string& argument : arguments)
                argv.push_back(const_cast<char*>(argument.c_str()));
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }

        close(outputPipe[1]);
        if (child < 0)
        {
            close(outputPipe[0]);
            setStatus("cannot start FFmpeg");
            return;
        }

        {
            lock_guard<mutex> lock(processMutex_);
            processId_ = child;
        }

        const int flags = fcntl(outputPipe[0], F_GETFL, 0);
        fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK);
        char buffer[8192];
        int waitStatus = 0;
        bool childExited = false;

        while (running_.load() && !childExited)
        {
            pollfd descriptor{};
            descriptor.fd = outputPipe[0];
            descriptor.events = POLLIN;
            if (poll(&descriptor, 1, 250) > 0 && (descriptor.revents & POLLIN))
            {
                const ssize_t count = read(outputPipe[0], buffer, sizeof(buffer));
                if (count > 0) processBytes(buffer, static_cast<size_t>(count));
            }
            childExited = waitpid(child, &waitStatus, WNOHANG) == child;
        }

        if (!childExited)
        {
            kill(child, SIGTERM);
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                if (waitpid(child, &waitStatus, WNOHANG) == child)
                {
                    childExited = true;
                    break;
                }
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        }
        if (!childExited)
        {
            kill(child, SIGKILL);
            waitpid(child, &waitStatus, 0);
        }
        close(outputPipe[0]);

        {
            lock_guard<mutex> lock(processMutex_);
            if (processId_ == child) processId_ = -1;
        }
        if (running_.load())
        {
            if (WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) == 127)
                setStatus("FFmpeg was not found; install the ffmpeg package");
            else
                setStatus("metadata track unavailable");
        }
    }
#endif

private:
    string rtspUrl_;
    atomic<bool> running_{ false };
    atomic<bool> streamConnected_{ false };
    atomic<uint64_t> bytesReceived_{ 0 };
    thread worker_;

    mutable mutex dataMutex_;
    vector<RawPersonBox> latestPersons_;
    Clock::time_point lastFrameTime_{};
    bool hasFrame_ = false;
    string status_;
    string xmlBuffer_;

    mutable mutex processMutex_;
#ifdef _WIN32
    HANDLE processHandle_ = nullptr;
#else
    pid_t processId_ = -1;
#endif
};

PersonMetadataReceiver::PersonMetadataReceiver()
    : impl_(new Impl())
{
}

PersonMetadataReceiver::~PersonMetadataReceiver() = default;

bool PersonMetadataReceiver::start(const string& rtspUrl)
{
    return impl_->start(rtspUrl);
}

void PersonMetadataReceiver::stop()
{
    impl_->stop();
}

PersonMetadataFrame PersonMetadataReceiver::snapshot(const Size& frameSize) const
{
    return impl_->snapshot(frameSize);
}
