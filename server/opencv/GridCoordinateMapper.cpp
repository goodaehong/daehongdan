#include "GridCoordinateMapper.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/objdetect/aruco_detector.hpp>)
#define DHD_ARUCO_NEW_API 1
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/objdetect/aruco_dictionary.hpp>
#else
#define DHD_ARUCO_NEW_API 0
#include <opencv2/aruco.hpp>
#endif

namespace
{
    bool isCommentOrEmpty(const std::string& line)
    {
        const std::size_t first = line.find_first_not_of(" \t\r\n");
        return first == std::string::npos || line[first] == '#';
    }

    int dictionaryId(const std::string& name)
    {
        static const std::map<std::string, int> dictionaries{
            {"DICT_4X4_50", cv::aruco::DICT_4X4_50},
            {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
            {"DICT_4X4_250", cv::aruco::DICT_4X4_250},
            {"DICT_5X5_50", cv::aruco::DICT_5X5_50},
            {"DICT_5X5_100", cv::aruco::DICT_5X5_100},
            {"DICT_5X5_250", cv::aruco::DICT_5X5_250},
            {"DICT_6X6_50", cv::aruco::DICT_6X6_50},
            {"DICT_6X6_100", cv::aruco::DICT_6X6_100},
            {"DICT_6X6_250", cv::aruco::DICT_6X6_250}
        };
        const auto found = dictionaries.find(name);
        return found == dictionaries.end() ? -1 : found->second;
    }

    bool finiteRect(const cv::Rect2f& rectangle)
    {
        return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
            std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
            rectangle.width > 0.0F && rectangle.height > 0.0F;
    }

    cv::Point2f normalizedPoint(const cv::Point2f& point, const cv::Size& size)
    {
        return cv::Point2f(
            point.x / static_cast<float>(std::max(1, size.width - 1)),
            point.y / static_cast<float>(std::max(1, size.height - 1)));
    }

    std::vector<cv::Point2f> boardWorldCorners(const cv::Rect2f& board)
    {
        return {
            {board.x, board.y},
            {board.x + board.width, board.y},
            {board.x + board.width, board.y + board.height},
            {board.x, board.y + board.height}
        };
    }

    bool finiteHomography(const cv::Mat& homography)
    {
        return !homography.empty() && homography.rows == 3 &&
            homography.cols == 3 && cv::checkRange(homography);
    }

    bool hasNonDegenerateHomographyBasis(
        const std::vector<cv::Point2f>& points)
    {
        auto triangleAreaTwice = [](const cv::Point2f& a,
            const cv::Point2f& b, const cv::Point2f& c)
        {
            return std::abs((b.x - a.x) * (c.y - a.y) -
                (b.y - a.y) * (c.x - a.x));
        };
        constexpr float MINIMUM_AREA_TWICE = 1.0e-4F;
        for (std::size_t a = 0; a < points.size(); ++a)
            for (std::size_t b = a + 1; b < points.size(); ++b)
                for (std::size_t c = b + 1; c < points.size(); ++c)
                    for (std::size_t d = c + 1; d < points.size(); ++d)
                    {
                        const std::vector<cv::Point2f> candidate{
                            points[a], points[b], points[c], points[d]
                        };
                        bool valid = true;
                        for (std::size_t i = 0; i < 4 && valid; ++i)
                            for (std::size_t j = i + 1; j < 4 && valid; ++j)
                                for (std::size_t k = j + 1; k < 4; ++k)
                                    if (triangleAreaTwice(candidate[i], candidate[j],
                                        candidate[k]) <= MINIMUM_AREA_TWICE)
                                    {
                                        valid = false;
                                        break;
                                    }
                        if (valid) return true;
                    }
        return false;
    }

    bool finiteCalibrationMatrix(const cv::Mat& matrix)
    {
        return !matrix.empty() && matrix.rows == 3 && matrix.cols == 3 &&
            cv::checkRange(matrix) && matrix.at<double>(0, 0) > 0.0 &&
            matrix.at<double>(1, 1) > 0.0;
    }

    bool plausibleCameraCalibration(
        const cv::Mat& matrix,
        const cv::Mat& distortion,
        const cv::Size& imageSize,
        double rms)
    {
        if (!finiteCalibrationMatrix(matrix) || distortion.empty() ||
            distortion.total() < 4 || !cv::checkRange(distortion) ||
            imageSize.width <= 0 || imageSize.height <= 0 ||
            !std::isfinite(rms) || rms < 0.0 || rms > 2.5)
            return false;

        const double focalX = matrix.at<double>(0, 0);
        const double focalY = matrix.at<double>(1, 1);
        const double principalX = matrix.at<double>(0, 2);
        const double principalY = matrix.at<double>(1, 2);
        const double focalLimit = std::max(imageSize.width, imageSize.height);
        if (std::abs(focalX / focalY - 1.0) > 0.10 ||
            focalX < focalLimit * 0.25 || focalX > focalLimit * 12.0 ||
            principalX < 0.0 || principalX >= imageSize.width ||
            principalY < 0.0 || principalY >= imageSize.height)
            return false;

        std::vector<cv::Point2f> samples{
            {0.0F, 0.0F}, {static_cast<float>(imageSize.width - 1), 0.0F},
            {static_cast<float>(imageSize.width - 1),
                static_cast<float>(imageSize.height - 1)},
            {0.0F, static_cast<float>(imageSize.height - 1)},
            {static_cast<float>(imageSize.width - 1) * 0.5F,
                static_cast<float>(imageSize.height - 1) * 0.5F}
        };
        std::vector<cv::Point2f> corrected;
        try
        {
            cv::undistortPoints(samples, corrected, matrix, distortion,
                cv::noArray(), matrix);
        }
        catch (const cv::Exception&)
        {
            return false;
        }
        if (corrected.size() != samples.size()) return false;
        double maximumShift = 0.0;
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            if (!std::isfinite(corrected[index].x) ||
                !std::isfinite(corrected[index].y))
                return false;
            maximumShift = std::max(maximumShift,
                static_cast<double>(cv::norm(samples[index] - corrected[index])));
        }
        const double imageDiagonal = std::hypot(
            static_cast<double>(imageSize.width),
            static_cast<double>(imageSize.height));
        return maximumShift <= imageDiagonal * 0.40;
    }

    bool calibrationSupportsFrame(
        const cv::Size& calibrationSize,
        const cv::Size& frameSize)
    {
        if (calibrationSize.width <= 0 || calibrationSize.height <= 0 ||
            frameSize.width <= 0 || frameSize.height <= 0)
            return false;
        const double calibrationAspect =
            static_cast<double>(calibrationSize.width) / calibrationSize.height;
        const double frameAspect =
            static_cast<double>(frameSize.width) / frameSize.height;
        return std::abs(calibrationAspect - frameAspect) <= 1e-3;
    }

    cv::Mat scaledCameraMatrix(
        const cv::Mat& cameraMatrix,
        const cv::Size& calibrationSize,
        const cv::Size& frameSize)
    {
        cv::Mat scaled = cameraMatrix.clone();
        const double scaleX = static_cast<double>(frameSize.width) /
            calibrationSize.width;
        const double scaleY = static_cast<double>(frameSize.height) /
            calibrationSize.height;
        scaled.at<double>(0, 0) *= scaleX;
        scaled.at<double>(0, 2) *= scaleX;
        scaled.at<double>(1, 1) *= scaleY;
        scaled.at<double>(1, 2) *= scaleY;
        return scaled;
    }

    bool undistortPixelPoints(
        const std::vector<cv::Point2f>& input,
        std::vector<cv::Point2f>& output,
        const cv::Mat& cameraMatrix,
        const cv::Mat& distortion,
        const cv::Size& calibrationSize,
        const cv::Size& frameSize)
    {
        if (input.empty() || !calibrationSupportsFrame(calibrationSize, frameSize))
            return false;
        const cv::Mat scaled = scaledCameraMatrix(
            cameraMatrix, calibrationSize, frameSize);
        cv::undistortPoints(input, output, scaled, distortion, cv::noArray(), scaled);
        return output.size() == input.size();
    }
}

bool GridCoordinateMapper::loadCameraCalibration(
    const std::string& calibrationPath,
    std::size_t channelIndex)
{
    cv::FileStorage input(calibrationPath, cv::FileStorage::READ);
    if (!input.isOpened())
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lensCalibrationConfigured_ = false;
        cameraCalibrationError_ = "cannot open camera calibration: " + calibrationPath;
        return false;
    }

    int version = 0;
    int logicalChannel = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    double rms = 0.0;
    cv::Mat cameraMatrix;
    cv::Mat distortion;
    input["version"] >> version;
    input["channel"] >> logicalChannel;
    input["image_width"] >> imageWidth;
    input["image_height"] >> imageHeight;
    input["camera_matrix"] >> cameraMatrix;
    input["distortion_coefficients"] >> distortion;
    input["reprojection_rms_px"] >> rms;
    if (cameraMatrix.type() != CV_64F) cameraMatrix.convertTo(cameraMatrix, CV_64F);
    if (distortion.type() != CV_64F) distortion.convertTo(distortion, CV_64F);

    const cv::Size calibrationSize(imageWidth, imageHeight);
    if (version != 1 || logicalChannel != static_cast<int>(channelIndex + 1) ||
        imageWidth <= 0 || imageHeight <= 0 ||
        !plausibleCameraCalibration(
            cameraMatrix, distortion, calibrationSize, rms))
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lensCalibrationConfigured_ = false;
        cameraCalibrationError_ =
            "invalid or wrong-channel camera calibration: " + calibrationPath;
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    cameraMatrix_ = cameraMatrix.clone();
    distortionCoefficients_ = distortion.clone();
    calibrationImageSize_ = cv::Size(imageWidth, imageHeight);
    lensCalibrationConfigured_ = true;
    lensCalibrationRmsPx_ = rms;
    cameraCalibrationError_.clear();
    homographyImageToWorld_.release();
    boardImagePolygon_.clear();
    homographyFrameSize_ = {};
    staticHomographyLoaded_ = false;
    lastGoodUpdate_ = {};
    status_.lensCalibrationConfigured = true;
    status_.lensCalibrationApplied = false;
    status_.lensCalibrationRmsPx = rms;
    return true;
}

std::string GridCoordinateMapper::configurationSignatureLocked() const
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(9)
        << "dictionary=" << dictionaryId_
        << ";channel=" << (channelIndex_ + 1)
        << ";scale=" << modelScale_
        << ";board=" << boardWorldM_.x << ',' << boardWorldM_.y << ','
        << boardWorldM_.width << ',' << boardWorldM_.height
        << ";factory=" << factoryWorldM_.x << ',' << factoryWorldM_.y << ','
        << factoryWorldM_.width << ',' << factoryWorldM_.height;
    for (const auto& entry : markers_)
    {
        const MarkerDefinition& marker = entry.second;
        output << ";marker=" << marker.id << ',' << marker.centerWorldM.x << ','
            << marker.centerWorldM.y << ',' << marker.sizeM << ','
            << marker.clockwiseRotationDegrees;
    }
    return output.str();
}

std::string GridCoordinateMapper::lensCalibrationSignatureLocked() const
{
    if (!lensCalibrationConfigured_) return "none";
    std::ostringstream output;
    output << std::fixed << std::setprecision(12)
        << calibrationImageSize_.width << 'x' << calibrationImageSize_.height;
    for (int row = 0; row < cameraMatrix_.rows; ++row)
        for (int column = 0; column < cameraMatrix_.cols; ++column)
            output << ',' << cameraMatrix_.at<double>(row, column);
    const cv::Mat flattened = distortionCoefficients_.reshape(1, 1);
    for (int column = 0; column < flattened.cols; ++column)
        output << ',' << flattened.at<double>(0, column);
    return output.str();
}

bool GridCoordinateMapper::saveStaticHomography(
    const std::string& homographyPath)
{
    cv::Mat homography;
    std::vector<cv::Point2f> boardPolygon;
    cv::Size frameSize;
    std::string configurationSignature;
    std::string lensSignature;
    double rms = 0.0;
    int logicalChannel = 0;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (!configured_ || !finiteHomography(homographyImageToWorld_) ||
            boardImagePolygon_.size() != 4 || homographyFrameSize_.width <= 0 ||
            homographyFrameSize_.height <= 0)
        {
            lock.unlock();
            std::unique_lock<std::shared_mutex> writeLock(mutex_);
            lastError_ = "cannot save static Homography before a valid ArUco update";
            return false;
        }
        homography = homographyImageToWorld_.clone();
        boardPolygon = boardImagePolygon_;
        frameSize = homographyFrameSize_;
        configurationSignature = configurationSignatureLocked();
        lensSignature = lensCalibrationSignatureLocked();
        rms = status_.reprojectionRmsPx;
        logicalChannel = static_cast<int>(channelIndex_ + 1);
    }

    namespace fs = std::filesystem;
    const fs::path target(homographyPath);
    const fs::path temporary = target.string() + ".tmp";
    const fs::path backup = target.string() + ".bak";
    std::error_code fileError;
    if (!target.parent_path().empty())
        fs::create_directories(target.parent_path(), fileError);
    fs::remove(temporary, fileError);
    fileError.clear();

    cv::FileStorage output(temporary.string(), cv::FileStorage::WRITE);
    if (!output.isOpened())
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "cannot create static Homography file: " + homographyPath;
        return false;
    }
    output << "version" << 1;
    output << "channel" << logicalChannel;
    output << "image_width" << frameSize.width;
    output << "image_height" << frameSize.height;
    output << "configuration_signature" << configurationSignature;
    output << "lens_calibration_signature" << lensSignature;
    output << "homography_image_to_world" << homography;
    output << "board_image_polygon" << boardPolygon;
    output << "reprojection_rms_px" << rms;
    output.release();

    fs::remove(backup, fileError);
    fileError.clear();
    const bool hadTarget = fs::exists(target);
    if (hadTarget)
    {
        fs::rename(target, backup, fileError);
        if (fileError)
        {
            fs::remove(temporary, fileError);
            std::unique_lock<std::shared_mutex> lock(mutex_);
            lastError_ = "cannot back up existing static Homography: " +
                fileError.message();
            return false;
        }
    }
    fs::rename(temporary, target, fileError);
    if (fileError)
    {
        if (hadTarget)
        {
            std::error_code restoreError;
            fs::rename(backup, target, restoreError);
        }
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "cannot publish static Homography: " + fileError.message();
        return false;
    }
    fs::remove(backup, fileError);
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_.clear();
    }
    return true;
}

bool GridCoordinateMapper::loadStaticHomography(
    const std::string& homographyPath,
    std::size_t channelIndex)
{
    cv::FileStorage input(homographyPath, cv::FileStorage::READ);
    if (!input.isOpened())
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "cannot open static Homography: " + homographyPath;
        return false;
    }

    int version = 0;
    int logicalChannel = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    double rms = 0.0;
    std::string configurationSignature;
    std::string lensSignature;
    cv::Mat homography;
    std::vector<cv::Point2f> boardPolygon;
    input["version"] >> version;
    input["channel"] >> logicalChannel;
    input["image_width"] >> imageWidth;
    input["image_height"] >> imageHeight;
    input["configuration_signature"] >> configurationSignature;
    input["lens_calibration_signature"] >> lensSignature;
    input["homography_image_to_world"] >> homography;
    input["board_image_polygon"] >> boardPolygon;
    input["reprojection_rms_px"] >> rms;
    if (homography.type() != CV_64F) homography.convertTo(homography, CV_64F);

    std::unique_lock<std::shared_mutex> lock(mutex_);
    const bool valid = configured_ && channelIndex == channelIndex_ &&
        version == 1 && logicalChannel == static_cast<int>(channelIndex + 1) &&
        imageWidth > 0 && imageHeight > 0 && finiteHomography(homography) &&
        boardPolygon.size() == 4 && std::isfinite(rms) && rms >= 0.0 &&
        rms <= maximumReprojectionRmsPx_ &&
        configurationSignature == configurationSignatureLocked() &&
        lensSignature == lensCalibrationSignatureLocked();
    if (!valid)
    {
        staticHomographyLoaded_ = false;
        lastError_ = "static Homography does not match channel, marker, or lens settings: " +
            homographyPath;
        return false;
    }

    homographyImageToWorld_ = homography.clone();
    boardImagePolygon_ = std::move(boardPolygon);
    homographyFrameSize_ = cv::Size(imageWidth, imageHeight);
    staticHomographyLoaded_ = true;
    lastGoodUpdate_ = Clock::now();
    lastError_.clear();
    status_.configured = true;
    status_.homographyValid = true;
    status_.homographyFresh = true;
    status_.staticHomography = true;
    status_.detectedMarkers = 0;
    status_.acceptedMarkers = 0;
    status_.inlierCorners = 0;
    status_.reprojectionRmsPx = rms;
    status_.homographyAgeMs = 0.0;
    status_.lensCalibrationConfigured = lensCalibrationConfigured_;
    status_.lensCalibrationApplied = lensCalibrationConfigured_;
    status_.lensCalibrationRmsPx = lensCalibrationRmsPx_;
    status_.message = "static Homography loaded";
    return true;
}

bool GridCoordinateMapper::hasStaticHomography() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return staticHomographyLoaded_ && finiteHomography(homographyImageToWorld_);
}

bool GridCoordinateMapper::loadArucoBoardConfiguration(
    const std::string& configurationPath,
    std::size_t channelIndex)
{
    std::ifstream input(configurationPath);
    if (!input)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        configured_ = false;
        lastError_ = "cannot open ArUco board configuration: " + configurationPath;
        status_ = {};
        status_.message = lastError_;
        return false;
    }

    std::string dictionaryName = "DICT_4X4_50";
    cv::Rect2f selectedBoard;
    bool selectedBoardFound = false;
    cv::Rect2f factoryWorld;
    bool factoryWorldFound = false;
    float modelScale = 1.0F;
    std::map<int, MarkerDefinition> selectedMarkers;
    int minimumVisibleMarkers = 2;
    int minimumInlierCorners = 8;
    double maximumReprojectionRmsPx = 5.0;
    int maximumHoldMs = 1500;
    std::uint64_t updateEveryFrames = 1;
    double smoothingAlpha = 0.45;

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (isCommentOrEmpty(line)) continue;

        std::istringstream parser(line);
        std::string command;
        parser >> command;
        if (command == "VERSION")
        {
            int version = 0;
            if (!(parser >> version) || version != 1)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "unsupported ArUco configuration version at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
        }
        else if (command == "DICTIONARY")
        {
            if (!(parser >> dictionaryName) || dictionaryId(dictionaryName) < 0)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "unknown ArUco dictionary at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
        }
        else if (command == "GRID")
        {
            int gridSize = 0;
            int usableMinimum = 0;
            int usableMaximum = 0;
            if (!(parser >> gridSize >> usableMinimum >> usableMaximum) ||
                gridSize != GRID_SIZE || usableMinimum != USABLE_MIN ||
                usableMaximum != USABLE_MAX)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "GRID must be 60 0 59 at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
        }
        else if (command == "QUALITY")
        {
            if (!(parser >> minimumVisibleMarkers >> minimumInlierCorners >>
                maximumReprojectionRmsPx >> maximumHoldMs >> updateEveryFrames >>
                smoothingAlpha) || minimumVisibleMarkers < 2 ||
                minimumInlierCorners < 8 || maximumReprojectionRmsPx <= 0.0 ||
                maximumHoldMs < 0 || updateEveryFrames < 1 ||
                smoothingAlpha <= 0.0 || smoothingAlpha > 1.0)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "invalid QUALITY at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
        }
        else if (command == "FACTORY")
        {
            float minimumX = 0.0F;
            float minimumY = 0.0F;
            float maximumX = 0.0F;
            float maximumY = 0.0F;
            if (!(parser >> minimumX >> minimumY >> maximumX >> maximumY) ||
                maximumX <= minimumX || maximumY <= minimumY)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "invalid FACTORY at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
            factoryWorld = cv::Rect2f(
                minimumX, minimumY, maximumX - minimumX, maximumY - minimumY);
            factoryWorldFound = true;
        }
        else if (command == "MODEL_SCALE")
        {
            if (!(parser >> modelScale) || !std::isfinite(modelScale) ||
                modelScale <= 0.0F)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "invalid MODEL_SCALE at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
        }
        else if (command == "BOARD")
        {
            int channelNumber = 0;
            float minimumX = 0.0F;
            float minimumY = 0.0F;
            float maximumX = 0.0F;
            float maximumY = 0.0F;
            if (!(parser >> channelNumber >> minimumX >> minimumY >> maximumX >> maximumY) ||
                channelNumber < 1 || maximumX <= minimumX || maximumY <= minimumY)
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "invalid BOARD at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
            if (channelNumber == static_cast<int>(channelIndex + 1))
            {
                selectedBoard = cv::Rect2f(
                    minimumX, minimumY, maximumX - minimumX, maximumY - minimumY);
                selectedBoardFound = true;
            }
        }
        else if (command == "MARKER")
        {
            int channelNumber = 0;
            MarkerDefinition marker;
            if (!(parser >> channelNumber >> marker.id >> marker.centerWorldM.x >>
                marker.centerWorldM.y >> marker.sizeM >>
                marker.clockwiseRotationDegrees) || channelNumber < 1 ||
                marker.id < 0 || marker.sizeM <= 0.0F ||
                !std::isfinite(marker.centerWorldM.x) ||
                !std::isfinite(marker.centerWorldM.y) ||
                !std::isfinite(marker.clockwiseRotationDegrees))
            {
                std::unique_lock<std::shared_mutex> lock(mutex_);
                configured_ = false;
                lastError_ = "invalid MARKER at line " +
                    std::to_string(lineNumber);
                status_ = {};
                status_.message = lastError_;
                return false;
            }
            if (channelNumber == static_cast<int>(channelIndex + 1))
            {
                if (!selectedMarkers.emplace(marker.id, marker).second)
                {
                    std::unique_lock<std::shared_mutex> lock(mutex_);
                    configured_ = false;
                    lastError_ = "duplicate marker ID for selected channel at line " +
                        std::to_string(lineNumber);
                    status_ = {};
                    status_.message = lastError_;
                    return false;
                }
            }
        }
        else
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            configured_ = false;
            lastError_ = "unknown ArUco configuration command at line " +
                std::to_string(lineNumber);
            status_ = {};
            status_.message = lastError_;
            return false;
        }
    }

    const int selectedDictionaryId = dictionaryId(dictionaryName);
    if (!factoryWorldFound)
        factoryWorld = selectedBoard;
    if (!selectedBoardFound || !finiteRect(selectedBoard) ||
        !finiteRect(factoryWorld) ||
        selectedMarkers.size() < static_cast<std::size_t>(minimumVisibleMarkers) ||
        selectedDictionaryId < 0)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        configured_ = false;
        lastError_ = "selected channel requires BOARD and enough MARKER entries";
        status_ = {};
        status_.message = lastError_;
        return false;
    }

    const float containmentTolerance = 1e-4F;
    if (selectedBoard.x < factoryWorld.x - containmentTolerance ||
        selectedBoard.y < factoryWorld.y - containmentTolerance ||
        selectedBoard.x + selectedBoard.width >
            factoryWorld.x + factoryWorld.width + containmentTolerance ||
        selectedBoard.y + selectedBoard.height >
            factoryWorld.y + factoryWorld.height + containmentTolerance)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        configured_ = false;
        lastError_ = "selected BOARD lies outside FACTORY";
        status_ = {};
        status_.message = lastError_;
        return false;
    }

    for (const auto& entry : selectedMarkers)
    {
        const MarkerDefinition& marker = entry.second;
        const float halfSize = marker.sizeM * modelScale * 0.5F;
        if (marker.centerWorldM.x < selectedBoard.x - halfSize ||
            marker.centerWorldM.x > selectedBoard.x + selectedBoard.width + halfSize ||
            marker.centerWorldM.y < selectedBoard.y - halfSize ||
            marker.centerWorldM.y > selectedBoard.y + selectedBoard.height + halfSize)
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            configured_ = false;
            lastError_ = "marker center lies outside selected BOARD";
            status_ = {};
            status_.message = lastError_;
            return false;
        }
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    dictionaryId_ = selectedDictionaryId;
    markers_ = std::move(selectedMarkers);
    boardWorldM_ = selectedBoard;
    factoryWorldM_ = factoryWorld;
    modelScale_ = modelScale;
    channelIndex_ = channelIndex;
    minimumVisibleMarkers_ = minimumVisibleMarkers;
    minimumInlierCorners_ = minimumInlierCorners;
    maximumReprojectionRmsPx_ = maximumReprojectionRmsPx;
    maximumHoldMs_ = maximumHoldMs;
    updateEveryFrames_ = updateEveryFrames;
    smoothingAlpha_ = smoothingAlpha;
    configured_ = true;
    homographyImageToWorld_.release();
    boardImagePolygon_.clear();
    staticHomographyLoaded_ = false;
    hasAttemptedFrame_ = false;
    lastGoodUpdate_ = {};
    lastError_.clear();
    status_ = {};
    status_.configured = true;
    status_.message = "ArUco board configured; waiting for visible markers";
    return true;
}

std::vector<cv::Point2f> GridCoordinateMapper::markerWorldCorners(
    const MarkerDefinition& marker,
    float modelScale) const
{
    const float half = marker.sizeM * modelScale * 0.5F;
    const float radians = marker.clockwiseRotationDegrees *
        static_cast<float>(CV_PI / 180.0);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const std::vector<cv::Point2f> local{
        {-half, -half}, {half, -half}, {half, half}, {-half, half}
    };
    std::vector<cv::Point2f> result;
    result.reserve(local.size());
    for (const cv::Point2f& point : local)
    {
        result.emplace_back(
            marker.centerWorldM.x + cosine * point.x - sine * point.y,
            marker.centerWorldM.y + sine * point.x + cosine * point.y);
    }
    return result;
}

bool GridCoordinateMapper::updateFromFrame(
    const cv::Mat& frame,
    std::uint64_t frameId)
{
    int selectedDictionaryId = -1;
    std::map<int, MarkerDefinition> markers;
    cv::Rect2f board;
    float modelScale = 1.0F;
    int minimumVisibleMarkers = 0;
    int minimumInlierCorners = 0;
    double maximumRms = 0.0;
    double smoothingAlpha = 1.0;
    bool applyLensCalibration = false;
    cv::Mat cameraMatrix;
    cv::Mat distortion;
    cv::Size calibrationSize;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (!configured_ || frame.empty()) return false;
        if (staticHomographyLoaded_) return false;
        if (hasAttemptedFrame_ && frameId >= lastAttemptFrameId_ &&
            frameId - lastAttemptFrameId_ < updateEveryFrames_)
            return false;
        hasAttemptedFrame_ = true;
        lastAttemptFrameId_ = frameId;
        selectedDictionaryId = dictionaryId_;
        markers = markers_;
        board = boardWorldM_;
        modelScale = modelScale_;
        minimumVisibleMarkers = minimumVisibleMarkers_;
        minimumInlierCorners = minimumInlierCorners_;
        maximumRms = maximumReprojectionRmsPx_;
        smoothingAlpha = smoothingAlpha_;
        applyLensCalibration = lensCalibrationConfigured_;
        if (applyLensCalibration)
        {
            cameraMatrix = cameraMatrix_.clone();
            distortion = distortionCoefficients_.clone();
            calibrationSize = calibrationImageSize_;
        }
    }

    std::vector<int> detectedIds;
    std::vector<std::vector<cv::Point2f>> detectedCorners;
    std::vector<std::vector<cv::Point2f>> rejectedCorners;
    try
    {
#if DHD_ARUCO_NEW_API
        cv::aruco::DetectorParameters parameters;
        parameters.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
        parameters.cornerRefinementWinSize = 5;
        const cv::aruco::Dictionary dictionary =
            cv::aruco::getPredefinedDictionary(selectedDictionaryId);
        const cv::aruco::ArucoDetector detector(dictionary, parameters);
        detector.detectMarkers(
            frame, detectedCorners, detectedIds, rejectedCorners);
#else
        cv::Ptr<cv::aruco::DetectorParameters> parameters =
            cv::aruco::DetectorParameters::create();
        parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
        parameters->cornerRefinementWinSize = 5;
        cv::aruco::detectMarkers(
            frame, cv::aruco::getPredefinedDictionary(selectedDictionaryId),
            detectedCorners, detectedIds, parameters, rejectedCorners);
#endif
    }
    catch (const cv::Exception& error)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = std::string("ArUco detection failed: ") + error.what();
        status_.detectedMarkers = 0;
        status_.acceptedMarkers = 0;
        status_.inlierCorners = 0;
        status_.message = lastError_;
        return false;
    }

    std::vector<std::vector<cv::Point2f>> correctedCorners = detectedCorners;
    if (applyLensCalibration)
    {
        if (!calibrationSupportsFrame(calibrationSize, frame.size()))
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            cameraCalibrationError_ =
                "camera calibration aspect ratio does not match the stream";
            status_.detectedMarkers = static_cast<int>(detectedIds.size());
            status_.acceptedMarkers = 0;
            status_.inlierCorners = 0;
            status_.message = cameraCalibrationError_;
            return false;
        }
        try
        {
            for (std::vector<cv::Point2f>& corners : correctedCorners)
            {
                std::vector<cv::Point2f> corrected;
                if (!undistortPixelPoints(corners, corrected, cameraMatrix,
                    distortion, calibrationSize, frame.size()))
                    throw std::runtime_error("point correction failed");
                corners = std::move(corrected);
            }
        }
        catch (const std::exception& error)
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            cameraCalibrationError_ =
                std::string("camera point undistortion failed: ") + error.what();
            status_.message = cameraCalibrationError_;
            return false;
        }
    }

    std::vector<cv::Point2f> imagePoints;
    std::vector<cv::Point2f> worldPoints;
    std::vector<int> pointMarkerIds;
    std::set<int> acceptedMarkerIds;

    // Establish the board orientation from marker centres first. ArUco corner
    // order follows each printed code, so papers installed at different
    // quarter-turns otherwise make an otherwise correct board look invalid.
    std::vector<cv::Point2f> centreWorldPoints;
    std::vector<cv::Point2f> centreImagePoints;
    for (std::size_t index = 0; index < detectedIds.size(); ++index)
    {
        const auto definition = markers.find(detectedIds[index]);
        if (definition == markers.end() || correctedCorners[index].size() != 4)
            continue;
        cv::Point2f centre;
        for (const cv::Point2f& corner : correctedCorners[index]) centre += corner;
        centre *= 0.25F;
        centreWorldPoints.push_back(definition->second.centerWorldM);
        centreImagePoints.push_back(normalizedPoint(centre, frame.size()));
    }
    cv::Mat centreWorldToImageHomography;
    if (centreWorldPoints.size() >= 4 &&
        hasNonDegenerateHomographyBasis(centreWorldPoints))
    {
        const double centreRansacThreshold = 3.0 /
            static_cast<double>(std::max(frame.cols, frame.rows));
        centreWorldToImageHomography = cv::findHomography(
            centreWorldPoints, centreImagePoints, cv::RANSAC,
            centreRansacThreshold, cv::noArray(), 1000, 0.995);
    }

    for (std::size_t index = 0; index < detectedIds.size(); ++index)
    {
        const auto definition = markers.find(detectedIds[index]);
        if (definition == markers.end() || correctedCorners[index].size() != 4)
            continue;
        MarkerDefinition orientedMarker = definition->second;
        std::vector<cv::Point2f> worldCorners =
            markerWorldCorners(orientedMarker, modelScale);
        if (finiteHomography(centreWorldToImageHomography))
        {
            double bestSquaredError = std::numeric_limits<double>::infinity();
            for (int quarterTurn = 0; quarterTurn < 4; ++quarterTurn)
            {
                orientedMarker.clockwiseRotationDegrees =
                    definition->second.clockwiseRotationDegrees +
                    static_cast<float>(quarterTurn * 90);
                const std::vector<cv::Point2f> candidateWorldCorners =
                    markerWorldCorners(orientedMarker, modelScale);
                std::vector<cv::Point2f> projectedCorners;
                cv::perspectiveTransform(candidateWorldCorners, projectedCorners,
                    centreWorldToImageHomography);
                double squaredError = 0.0;
                for (std::size_t corner = 0; corner < 4; ++corner)
                {
                    const cv::Point2f detected = normalizedPoint(
                        correctedCorners[index][corner], frame.size());
                    const cv::Point2f delta = projectedCorners[corner] - detected;
                    squaredError += delta.dot(delta);
                }
                if (squaredError < bestSquaredError)
                {
                    bestSquaredError = squaredError;
                    worldCorners = candidateWorldCorners;
                }
            }
        }
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            imagePoints.push_back(normalizedPoint(correctedCorners[index][corner], frame.size()));
            worldPoints.push_back(worldCorners[corner]);
            pointMarkerIds.push_back(detectedIds[index]);
        }
        acceptedMarkerIds.insert(detectedIds[index]);
    }

    if (acceptedMarkerIds.size() < static_cast<std::size_t>(minimumVisibleMarkers) ||
        imagePoints.size() < static_cast<std::size_t>(minimumInlierCorners))
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "not enough configured ArUco markers are visible";
        status_.detectedMarkers = static_cast<int>(detectedIds.size());
        status_.acceptedMarkers = static_cast<int>(acceptedMarkerIds.size());
        status_.inlierCorners = 0;
        status_.message = lastError_;
        return false;
    }

    cv::Mat inlierMask;
    const double normalizedRansacThreshold =
        std::max(3.0, maximumRms) /
        static_cast<double>(std::max(frame.cols, frame.rows));
    // Estimate world -> normalized image so the RANSAC threshold remains in
    // image units regardless of FACTORY metres or MODEL_SCALE.
    cv::Mat worldToImageHomography = cv::findHomography(
        worldPoints, imagePoints, cv::RANSAC, normalizedRansacThreshold,
        inlierMask, 2000, 0.995);
    if (!finiteHomography(worldToImageHomography))
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "visible ArUco corners do not form a valid Homography";
        status_.detectedMarkers = static_cast<int>(detectedIds.size());
        status_.acceptedMarkers = static_cast<int>(acceptedMarkerIds.size());
        status_.inlierCorners = 0;
        status_.message = lastError_;
        return false;
    }

    int inlierCorners = 0;
    std::map<int, int> inlierCornersPerMarker;
    for (int index = 0; index < inlierMask.rows; ++index)
    {
        if (inlierMask.at<unsigned char>(index, 0) == 0) continue;
        ++inlierCorners;
        ++inlierCornersPerMarker[pointMarkerIds[static_cast<std::size_t>(index)]];
    }
    int inlierMarkers = 0;
    for (const auto& entry : inlierCornersPerMarker)
        if (entry.second >= 3) ++inlierMarkers;
    if (inlierCorners < minimumInlierCorners || inlierMarkers < minimumVisibleMarkers)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "RANSAC rejected too many ArUco marker corners";
        status_.detectedMarkers = static_cast<int>(detectedIds.size());
        status_.acceptedMarkers = static_cast<int>(acceptedMarkerIds.size());
        status_.inlierCorners = inlierCorners;
        status_.message = lastError_;
        return false;
    }

    cv::Mat homography = worldToImageHomography.inv();
    if (!finiteHomography(homography))
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "accepted ArUco Homography is not invertible";
        status_.message = lastError_;
        return false;
    }
    std::vector<cv::Point2f> projectedImagePoints;
    cv::perspectiveTransform(
        worldPoints, projectedImagePoints, worldToImageHomography);
    double squaredPixelError = 0.0;
    int errorSamples = 0;
    for (std::size_t index = 0; index < imagePoints.size(); ++index)
    {
        if (inlierMask.at<unsigned char>(static_cast<int>(index), 0) == 0) continue;
        const double deltaX = (projectedImagePoints[index].x - imagePoints[index].x) *
            static_cast<double>(std::max(1, frame.cols - 1));
        const double deltaY = (projectedImagePoints[index].y - imagePoints[index].y) *
            static_cast<double>(std::max(1, frame.rows - 1));
        squaredPixelError += deltaX * deltaX + deltaY * deltaY;
        ++errorSamples;
    }
    const double rms = errorSamples > 0 ?
        std::sqrt(squaredPixelError / static_cast<double>(errorSamples)) :
        std::numeric_limits<double>::infinity();
    if (!std::isfinite(rms) || rms > maximumRms)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "ArUco Homography reprojection error is too high";
        status_.detectedMarkers = static_cast<int>(detectedIds.size());
        status_.acceptedMarkers = static_cast<int>(acceptedMarkerIds.size());
        status_.inlierCorners = inlierCorners;
        status_.reprojectionRmsPx = rms;
        status_.message = lastError_;
        return false;
    }

    const std::vector<cv::Point2f> boardCornersWorld = boardWorldCorners(board);
    std::vector<cv::Point2f> newBoardImagePolygon;
    cv::perspectiveTransform(
        boardCornersWorld, newBoardImagePolygon, worldToImageHomography);
    if (newBoardImagePolygon.size() != 4)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastError_ = "failed to project the configured board boundary";
        status_.message = lastError_;
        return false;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (boardImagePolygon_.size() == 4 &&
            homographyIsFreshLocked(Clock::now()) && smoothingAlpha < 1.0)
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                newBoardImagePolygon[index] =
                    boardImagePolygon_[index] * static_cast<float>(1.0 - smoothingAlpha) +
                    newBoardImagePolygon[index] * static_cast<float>(smoothingAlpha);
            }
            homography = cv::getPerspectiveTransform(
                newBoardImagePolygon, boardCornersWorld);
            if (!finiteHomography(homography))
            {
                lastError_ = "smoothed ArUco Homography is invalid";
                status_.message = lastError_;
                return false;
            }
        }
        homographyImageToWorld_ = homography.clone();
        boardImagePolygon_ = std::move(newBoardImagePolygon);
        homographyFrameSize_ = frame.size();
        lastGoodUpdate_ = Clock::now();
        lastError_.clear();
        cameraCalibrationError_.clear();
        status_.configured = true;
        status_.homographyValid = true;
        status_.homographyFresh = true;
        status_.staticHomography = false;
        status_.detectedMarkers = static_cast<int>(detectedIds.size());
        status_.acceptedMarkers = static_cast<int>(acceptedMarkerIds.size());
        status_.inlierCorners = inlierCorners;
        status_.reprojectionRmsPx = rms;
        status_.homographyAgeMs = 0.0;
        status_.lensCalibrationConfigured = lensCalibrationConfigured_;
        status_.lensCalibrationApplied = lensCalibrationConfigured_;
        status_.lensCalibrationRmsPx = lensCalibrationRmsPx_;
        status_.message = "ArUco Homography updated";
    }
    return true;
}

bool GridCoordinateMapper::homographyIsFreshLocked(Clock::time_point now) const
{
    if (homographyImageToWorld_.empty() || lastGoodUpdate_ == Clock::time_point{})
        return false;
    if (staticHomographyLoaded_) return true;
    const double ageMs = std::chrono::duration<double, std::milli>(
        now - lastGoodUpdate_).count();
    return ageMs <= static_cast<double>(maximumHoldMs_);
}

bool GridCoordinateMapper::map(
    const cv::Point2f& normalizedImagePoint,
    cv::Point& gridPoint,
    cv::Point2f* factoryWorldPointM) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!configured_ || !homographyIsFreshLocked(Clock::now()) ||
        !std::isfinite(normalizedImagePoint.x) ||
        !std::isfinite(normalizedImagePoint.y) ||
        normalizedImagePoint.x < 0.0F || normalizedImagePoint.x > 1.0F ||
        normalizedImagePoint.y < 0.0F || normalizedImagePoint.y > 1.0F)
        return false;

    cv::Point2f correctedNormalizedPoint = normalizedImagePoint;
    if (lensCalibrationConfigured_)
    {
        if (homographyFrameSize_.width <= 0 || homographyFrameSize_.height <= 0)
            return false;
        const std::vector<cv::Point2f> distortedPixelPoint{
            cv::Point2f(
                normalizedImagePoint.x * std::max(1, homographyFrameSize_.width - 1),
                normalizedImagePoint.y * std::max(1, homographyFrameSize_.height - 1))
        };
        std::vector<cv::Point2f> correctedPixelPoint;
        try
        {
            if (!undistortPixelPoints(distortedPixelPoint, correctedPixelPoint,
                cameraMatrix_, distortionCoefficients_, calibrationImageSize_,
                homographyFrameSize_))
                return false;
        }
        catch (const cv::Exception&)
        {
            return false;
        }
        correctedNormalizedPoint = normalizedPoint(
            correctedPixelPoint.front(), homographyFrameSize_);
    }

    std::vector<cv::Point2f> source{correctedNormalizedPoint};
    std::vector<cv::Point2f> world;
    cv::perspectiveTransform(source, world, homographyImageToWorld_);
    if (world.size() != 1 || !std::isfinite(world[0].x) ||
        !std::isfinite(world[0].y))
        return false;

    const float worldToleranceM = std::max(
        0.002F, std::max(boardWorldM_.width, boardWorldM_.height) * 0.005F);
    if (world[0].x < boardWorldM_.x - worldToleranceM ||
        world[0].x > boardWorldM_.x + boardWorldM_.width + worldToleranceM ||
        world[0].y < boardWorldM_.y - worldToleranceM ||
        world[0].y > boardWorldM_.y + boardWorldM_.height + worldToleranceM)
        return false;

    const double normalizedWorldX =
        (world[0].x - factoryWorldM_.x) / factoryWorldM_.width;
    const double normalizedWorldY =
        (world[0].y - factoryWorldM_.y) / factoryWorldM_.height;
    gridPoint.x = std::clamp(
        static_cast<int>(std::lround(
            USABLE_MIN + normalizedWorldX * (USABLE_MAX - USABLE_MIN))),
        USABLE_MIN, USABLE_MAX);
    gridPoint.y = std::clamp(
        static_cast<int>(std::lround(
            USABLE_MIN + normalizedWorldY * (USABLE_MAX - USABLE_MIN))),
        USABLE_MIN, USABLE_MAX);
    if (factoryWorldPointM != nullptr)
        *factoryWorldPointM = world[0];
    return true;
}

void GridCoordinateMapper::resetTracking()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    hasAttemptedFrame_ = false;
    if (staticHomographyLoaded_)
    {
        lastGoodUpdate_ = Clock::now();
        status_.configured = configured_;
        status_.homographyValid = true;
        status_.homographyFresh = true;
        status_.staticHomography = true;
        status_.detectedMarkers = 0;
        status_.acceptedMarkers = 0;
        status_.inlierCorners = 0;
        status_.homographyAgeMs = 0.0;
        status_.message = "static Homography retained after stream reset";
        return;
    }
    homographyImageToWorld_.release();
    boardImagePolygon_.clear();
    homographyFrameSize_ = {};
    lastGoodUpdate_ = {};
    if (configured_)
    {
        status_ = {};
        status_.configured = true;
        status_.message = "waiting for ArUco markers after stream reset";
    }
}

bool GridCoordinateMapper::isConfigured() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return configured_;
}

bool GridCoordinateMapper::isReady() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return configured_ && homographyIsFreshLocked(Clock::now());
}

ArucoMappingStatus GridCoordinateMapper::status() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    ArucoMappingStatus result = status_;
    const Clock::time_point now = Clock::now();
    result.homographyValid = !homographyImageToWorld_.empty();
    result.homographyFresh = configured_ && homographyIsFreshLocked(now);
    result.staticHomography = staticHomographyLoaded_;
    if (lastGoodUpdate_ != Clock::time_point{})
        result.homographyAgeMs = std::chrono::duration<double, std::milli>(
            now - lastGoodUpdate_).count();
    if (staticHomographyLoaded_) result.homographyAgeMs = 0.0;
    result.lensCalibrationConfigured = lensCalibrationConfigured_;
    result.lensCalibrationApplied = lensCalibrationConfigured_ &&
        !homographyImageToWorld_.empty() && homographyFrameSize_.width > 0;
    result.lensCalibrationRmsPx = lensCalibrationRmsPx_;
    return result;
}

std::string GridCoordinateMapper::lastError() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lastError_;
}

std::string GridCoordinateMapper::cameraCalibrationError() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return cameraCalibrationError_;
}
