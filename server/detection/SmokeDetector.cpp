#include "SmokeDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

#include "AppConfig.h"

#ifndef SMOKE_HAS_NCNN
#define SMOKE_HAS_NCNN 0
#endif

#if SMOKE_HAS_NCNN
#include <net.h>
#endif

namespace
{
    // 상대 모델 경로는 현재 작업 폴더와 실행 파일 폴더 순서로 확인한다.
    std::filesystem::path executableDirectory()
    {
#ifdef _WIN32
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) return {};
        return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
#else
        char buffer[PATH_MAX + 1] = {};
        const ssize_t length = readlink("/proc/self/exe", buffer, PATH_MAX);
        if (length <= 0) return {};
        buffer[length] = '\0';
        return std::filesystem::path(buffer).parent_path();
#endif
    }

    std::string resolveModelPath(const std::string& requested)
    {
        const std::filesystem::path requestedPath(requested);
        std::error_code error;
        if (std::filesystem::is_regular_file(requestedPath, error))
            return requestedPath.string();

        const std::filesystem::path executablePath =
            executableDirectory() / requestedPath;
        error.clear();
        if (std::filesystem::is_regular_file(executablePath, error))
            return executablePath.string();

        return requested;
    }

    struct CandidateBox
    {
        cv::Rect box;
        float score = 0.0F;
    };

    float intersectionOverUnion(const cv::Rect& a, const cv::Rect& b)
    {
        const cv::Rect intersection = a & b;
        if (intersection.empty()) return 0.0F;
        const float intersectionArea = static_cast<float>(intersection.area());
        const float unionArea = static_cast<float>(a.area() + b.area()) - intersectionArea;
        return unionArea > 0.0F ? intersectionArea / unionArea : 0.0F;
    }

    // 같은 연기 영역에 겹친 저점수 박스를 NMS로 제거한다.
    std::vector<CandidateBox> nonMaximumSuppression(std::vector<CandidateBox> candidates)
    {
        std::sort(candidates.begin(), candidates.end(), [](const CandidateBox& a, const CandidateBox& b) {
            return a.score > b.score;
        });

        std::vector<CandidateBox> kept;
        for (const CandidateBox& candidate : candidates)
        {
            bool overlaps = false;
            for (const CandidateBox& existing : kept)
            {
                if (intersectionOverUnion(candidate.box, existing.box) > smoke_config::NMS_THRESHOLD)
                {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) kept.push_back(candidate);
        }
        return kept;
    }
}

class SmokeDetector::Impl
{
public:
    bool load(const std::string& paramPath, const std::string& binPath)
    {
        ready_ = false;
        lastError_.clear();

#if !SMOKE_HAS_NCNN
        (void)paramPath;
        (void)binPath;
        lastError_ = "Smoke detection was built without NCNN (SMOKE_HAS_NCNN=0).";
        return false;
#else
        net_.clear();
        // Raspberry Pi 4 CPU 배포를 기준으로 Vulkan은 사용하지 않는다.
        net_.opt.use_vulkan_compute = false;
        net_.opt.num_threads = smoke_config::NCNN_NUM_THREADS;

        const std::string resolvedParamPath = resolveModelPath(paramPath);
        const std::string resolvedBinPath = resolveModelPath(binPath);

        if (net_.load_param(resolvedParamPath.c_str()) != 0)
        {
            lastError_ = "Failed to load NCNN param: " + resolvedParamPath;
            return false;
        }
        if (net_.load_model(resolvedBinPath.c_str()) != 0)
        {
            lastError_ = "Failed to load NCNN bin: " + resolvedBinPath;
            net_.clear();
            return false;
        }

        ready_ = true;
        return true;
#endif
    }

    SmokeDetectionResult detect(const cv::Mat& inputFrame)
    {
        SmokeDetectionResult result;
        result.modelReady = ready_;
        if (!ready_)
        {
            result.error = lastError_;
            return result;
        }
        if (inputFrame.empty())
        {
            result.error = "SmokeDetector received an empty frame.";
            return result;
        }

#if !SMOKE_HAS_NCNN
        result.error = lastError_;
        return result;
#else
        cv::Mat bgr;
        if (inputFrame.channels() == 3)
            bgr = inputFrame;
        else if (inputFrame.channels() == 4)
            cv::cvtColor(inputFrame, bgr, cv::COLOR_BGRA2BGR);
        else
            cv::cvtColor(inputFrame, bgr, cv::COLOR_GRAY2BGR);

        // 종횡비를 유지한 letterbox로 학습/내보내기 입력 크기와 맞춘다.
        const float scale = std::min(
            static_cast<float>(smoke_config::INPUT_WIDTH) / static_cast<float>(bgr.cols),
            static_cast<float>(smoke_config::INPUT_HEIGHT) / static_cast<float>(bgr.rows));
        const int resizedWidth = std::max(1, static_cast<int>(std::round(bgr.cols * scale)));
        const int resizedHeight = std::max(1, static_cast<int>(std::round(bgr.rows * scale)));
        const int padWidth = smoke_config::INPUT_WIDTH - resizedWidth;
        const int padHeight = smoke_config::INPUT_HEIGHT - resizedHeight;
        const int left = padWidth / 2;
        const int right = padWidth - left;
        const int top = padHeight / 2;
        const int bottom = padHeight - top;

        cv::Mat resized, letterboxed;
        const int interpolation = scale < 1.0F ? cv::INTER_AREA : cv::INTER_LINEAR;
        cv::resize(bgr, resized, cv::Size(resizedWidth, resizedHeight), 0.0, 0.0, interpolation);
        cv::copyMakeBorder(
            resized, letterboxed, top, bottom, left, right,
            cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));

        ncnn::Mat input = ncnn::Mat::from_pixels(
            letterboxed.data,
            ncnn::Mat::PIXEL_BGR2RGB,
            smoke_config::INPUT_WIDTH,
            smoke_config::INPUT_HEIGHT);
        constexpr float NORMALIZE[3] = { 1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F };
        input.substract_mean_normalize(nullptr, NORMALIZE);

        ncnn::Extractor extractor = net_.create_extractor();
        if (extractor.input(smoke_config::INPUT_BLOB_NAME, input) != 0)
        {
            result.error = "Failed to set NCNN input blob. Check INPUT_BLOB_NAME.";
            return result;
        }

        ncnn::Mat output;
        if (extractor.extract(smoke_config::OUTPUT_BLOB_NAME, output) != 0 || output.empty())
        {
            result.error = "Failed to extract NCNN output blob. Check OUTPUT_BLOB_NAME.";
            return result;
        }

        // Ultralytics 출력은 [1, 4+클래스 수, 예측 수]이며 NCNN은 보통 batch 축을 제거한다.
        // 내보내기 버전에 따른 행/열/채널 배치를 모두 허용한다.
        ncnn::Mat matrix = output;
        if (output.dims == 3 && output.c == 1)
            matrix = output.channel(0);

        int attributeCount = 0;
        int predictionCount = 0;
        bool attributesInRows = true;
        bool attributesInChannels = false;

        if (matrix.dims == 2 && matrix.h >= 5 && matrix.h <= 16)
        {
            attributeCount = matrix.h;
            predictionCount = matrix.w;
            attributesInRows = true;
        }
        else if (matrix.dims == 2 && matrix.w >= 5 && matrix.w <= 16)
        {
            attributeCount = matrix.w;
            predictionCount = matrix.h;
            attributesInRows = false;
        }
        else if (output.dims == 3 && output.c >= 5 && output.c <= 16 && output.h == 1)
        {
            attributeCount = output.c;
            predictionCount = output.w;
            attributesInChannels = true;
        }
        else
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "Unexpected NCNN output shape: dims=%d w=%d h=%d c=%d",
                output.dims, output.w, output.h, output.c);
            result.error = message;
            return result;
        }

        // fire 클래스 점수는 읽지 않고 설정된 smoke 클래스 열만 디코딩한다.
        const int smokeScoreAttribute = 4 + smoke_config::SMOKE_CLASS_ID;
        if (attributeCount <= smokeScoreAttribute)
        {
            result.error =
                "NCNN output does not contain the configured smoke class.";
            return result;
        }

        const auto valueAt = [&](int attribute, int index) -> float {
            if (attributesInChannels)
                return output.channel(attribute).row(0)[index];
            if (attributesInRows)
                return matrix.row(attribute)[index];
            return matrix.row(index)[attribute];
        };

        std::vector<CandidateBox> candidates;
        candidates.reserve(32);
        for (int index = 0; index < predictionCount; ++index)
        {
            const float score = valueAt(smokeScoreAttribute, index);
            result.maxScore = std::max(result.maxScore, static_cast<double>(score));
            if (score < smoke_config::RAW_CANDIDATE_THRESHOLD) continue;

            const float centerX = valueAt(0, index);
            const float centerY = valueAt(1, index);
            const float width = valueAt(2, index);
            const float height = valueAt(3, index);

            // letterbox 패딩을 제거한 뒤 박스를 원본 프레임 좌표로 되돌린다.
            float x1 = (centerX - width * 0.5F - static_cast<float>(left)) / scale;
            float y1 = (centerY - height * 0.5F - static_cast<float>(top)) / scale;
            float x2 = (centerX + width * 0.5F - static_cast<float>(left)) / scale;
            float y2 = (centerY + height * 0.5F - static_cast<float>(top)) / scale;
            x1 = std::clamp(x1, 0.0F, static_cast<float>(bgr.cols - 1));
            y1 = std::clamp(y1, 0.0F, static_cast<float>(bgr.rows - 1));
            x2 = std::clamp(x2, 0.0F, static_cast<float>(bgr.cols));
            y2 = std::clamp(y2, 0.0F, static_cast<float>(bgr.rows));

            const int boxX = static_cast<int>(std::floor(x1));
            const int boxY = static_cast<int>(std::floor(y1));
            const int boxWidth = static_cast<int>(std::ceil(x2)) - boxX;
            const int boxHeight = static_cast<int>(std::ceil(y2)) - boxY;
            if (boxWidth <= 1 || boxHeight <= 1) continue;
            candidates.push_back({ cv::Rect(boxX, boxY, boxWidth, boxHeight), score });
        }

        const std::vector<CandidateBox> kept = nonMaximumSuppression(std::move(candidates));
        result.boxes.reserve(kept.size());
        for (const CandidateBox& candidate : kept)
        {
            char label[48];
            std::snprintf(label, sizeof(label), "smoke %.2f", candidate.score);
            DetectionBox box;
            box.box = candidate.box;
            box.label = label;
            box.type = DetectionType::SMOKE;
            box.score = candidate.score;
            box.normalizedX = std::clamp(
                static_cast<double>(box.box.x) / bgr.cols, 0.0, 1.0);
            box.normalizedY = std::clamp(
                static_cast<double>(box.box.y) / bgr.rows, 0.0, 1.0);
            box.normalizedWidth = std::clamp(
                static_cast<double>(box.box.width) / bgr.cols, 0.0, 1.0);
            box.normalizedHeight = std::clamp(
                static_cast<double>(box.box.height) / bgr.rows, 0.0, 1.0);
            result.boxes.push_back(std::move(box));
        }
        result.candidate = !result.boxes.empty();
        return result;
#endif
    }

    bool isReady() const { return ready_; }
    std::string lastError() const { return lastError_; }

private:
    bool ready_ = false;
    std::string lastError_;
#if SMOKE_HAS_NCNN
    ncnn::Net net_;
#endif
};

SmokeDetector::SmokeDetector() : impl_(new Impl()) {}
SmokeDetector::~SmokeDetector() = default;

bool SmokeDetector::load(const std::string& paramPath, const std::string& binPath)
{
    return impl_->load(paramPath, binPath);
}

SmokeDetectionResult SmokeDetector::detect(const cv::Mat& inputFrame)
{
    return impl_->detect(inputFrame);
}

bool SmokeDetector::isReady() const { return impl_->isReady(); }
std::string SmokeDetector::lastError() const { return impl_->lastError(); }
