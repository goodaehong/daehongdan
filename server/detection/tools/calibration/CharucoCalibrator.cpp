// 렌즈 왜곡 보정 도구. ChArUco 보드를 여러 자세로 촬영해 카메라 내부 파라미터를 구한다.
// 결과는 camera_calibration_chN.yml 로 저장되고 서버가 기동 시 읽는다.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#if __has_include(<opencv2/objdetect/charuco_detector.hpp>)
#define DHD_CHARUCO_NEW_API 1
#include <opencv2/objdetect/aruco_board.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#else
#define DHD_CHARUCO_NEW_API 0
#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#endif
#include <opencv2/videoio.hpp>

namespace
{
    constexpr int BOARD_SQUARES_X = 7;
    constexpr int BOARD_SQUARES_Y = 5;
    constexpr float SQUARE_LENGTH_M = 0.050F;
    constexpr float MARKER_LENGTH_M = 0.035F;
    constexpr int MIN_CAPTURED_CORNERS = 10;
    constexpr int MIN_CALIBRATION_VIEWS = 20;
    constexpr int RECOMMENDED_CALIBRATION_VIEWS = 25;
    constexpr double MIN_IMAGE_COVERAGE = 0.55;
    constexpr double MIN_SCALE_VARIATION = 1.25;

    struct Options
    {
        std::string source;
        std::string output = "camera_calibration_ch4.yml";
        bool headless = false;
        bool probeOnly = false;
        bool arucoOnly = false;
        bool legacyPattern = false;
        std::uint64_t maxFrames = 0;
        int channel = 4;
    };

    struct CapturedView
    {
        std::vector<cv::Point3f> objectPoints;
        std::vector<cv::Point2f> imagePoints;
        int charucoCorners = 0;
    };

    void printUsage()
    {
        std::cout
            << "Usage: charuco_calibrator <video-or-rtsp> [options]\n"
            << "  --output <file.yml>     output calibration file\n"
            << "  --channel 4             logical lens/channel number\n"
            << "  --probe-only            detect the board without saving views\n"
            << "  --aruco-only            list any DICT_4X4_50 marker IDs\n"
            << "  --headless              no preview window (probe mode)\n"
            << "  --max-frames <count>    probe frame limit (default 180)\n"
            << "  --legacy-pattern        board was generated with OpenCV < 4.6\n\n"
            << "Board defaults: DICT_4X4_50, 7x5 squares, 50 mm square, "
            << "35 mm marker, 17 markers.\n"
            << "Interactive keys: SPACE capture, U undo, C calibrate/save, Q/ESC quit.\n";
    }

    Options parseOptions(int argc, char** argv)
    {
        if (argc == 2 &&
            (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
        {
            printUsage();
            std::exit(0);
        }
        if (argc < 2)
        {
            printUsage();
            throw std::runtime_error("input source is required");
        }

        Options options;
        options.source = argv[1];
        for (int index = 2; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--output" && index + 1 < argc)
                options.output = argv[++index];
            else if (argument == "--channel" && index + 1 < argc)
                options.channel = std::stoi(argv[++index]);
            else if (argument == "--max-frames" && index + 1 < argc)
                options.maxFrames = std::stoull(argv[++index]);
            else if (argument == "--probe-only")
                options.probeOnly = true;
            else if (argument == "--aruco-only")
            {
                options.arucoOnly = true;
                options.probeOnly = true;
            }
            else if (argument == "--headless")
                options.headless = true;
            else if (argument == "--legacy-pattern")
                options.legacyPattern = true;
            else if (argument == "--help" || argument == "-h")
            {
                printUsage();
                std::exit(0);
            }
            else
                throw std::runtime_error("unknown or incomplete option: " + argument);
        }
        if (options.channel < 1)
            throw std::runtime_error("channel must be positive");
        if (options.headless && !options.probeOnly)
            throw std::runtime_error("--headless currently requires --probe-only");
        if (options.probeOnly && options.maxFrames == 0)
            options.maxFrames = 180;
        return options;
    }

    bool isNetworkSource(const std::string& source)
    {
        std::string lower = source;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return lower.rfind("rtsp://", 0) == 0 || lower.rfind("rtsps://", 0) == 0;
    }

    cv::VideoCapture openCapture(const std::string& source)
    {
        cv::VideoCapture capture;
        if (isNetworkSource(source))
        {
            const std::vector<int> parameters{
                cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                cv::CAP_PROP_READ_TIMEOUT_MSEC, 2500
            };
            capture.open(source, cv::CAP_FFMPEG, parameters);
            capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        }
        else if (!source.empty() && std::all_of(source.begin(), source.end(),
            [](unsigned char value) { return std::isdigit(value) != 0; }))
        {
#if defined(_WIN32)
            capture.open(std::stoi(source), cv::CAP_DSHOW);
#else
            capture.open(std::stoi(source));
#endif
        }
        else
            capture.open(source);
        return capture;
    }

    std::vector<double> perViewErrors(
        const std::vector<CapturedView>& views,
        const cv::Mat& cameraMatrix,
        const cv::Mat& distortion,
        const std::vector<cv::Mat>& rotations,
        const std::vector<cv::Mat>& translations)
    {
        std::vector<double> errors;
        errors.reserve(views.size());
        for (std::size_t index = 0; index < views.size(); ++index)
        {
            std::vector<cv::Point2f> projected;
            cv::projectPoints(views[index].objectPoints, rotations[index],
                translations[index], cameraMatrix, distortion, projected);
            errors.push_back(cv::norm(views[index].imagePoints, projected, cv::NORM_L2) /
                std::sqrt(static_cast<double>(projected.size())));
        }
        return errors;
    }

    bool captureSetIsDiverse(
        const std::vector<CapturedView>& views,
        const cv::Size& imageSize,
        std::string& reason)
    {
        float minimumX = static_cast<float>(imageSize.width);
        float minimumY = static_cast<float>(imageSize.height);
        float maximumX = 0.0F;
        float maximumY = 0.0F;
        double minimumArea = std::numeric_limits<double>::max();
        double maximumArea = 0.0;
        for (const CapturedView& view : views)
        {
            if (view.imagePoints.empty()) continue;
            const cv::Rect bounds = cv::boundingRect(view.imagePoints);
            minimumArea = std::min(minimumArea, static_cast<double>(bounds.area()));
            maximumArea = std::max(maximumArea, static_cast<double>(bounds.area()));
            for (const cv::Point2f& point : view.imagePoints)
            {
                minimumX = std::min(minimumX, point.x);
                minimumY = std::min(minimumY, point.y);
                maximumX = std::max(maximumX, point.x);
                maximumY = std::max(maximumY, point.y);
            }
        }

        const double coverageX = (maximumX - minimumX) / imageSize.width;
        const double coverageY = (maximumY - minimumY) / imageSize.height;
        if (coverageX < MIN_IMAGE_COVERAGE || coverageY < MIN_IMAGE_COVERAGE)
        {
            std::ostringstream message;
            message << std::fixed << std::setprecision(2)
                << "board observations cover only " << coverageX * 100.0 << "% x "
                << coverageY * 100.0
                << "% of the frame; move the board closer to every edge";
            reason = message.str();
            return false;
        }
        if (minimumArea <= 0.0 || maximumArea / minimumArea < MIN_SCALE_VARIATION)
        {
            reason = "board scale is too similar in every view; include closer and farther poses";
            return false;
        }
        return true;
    }

    double maximumUndistortShift(
        const cv::Mat& cameraMatrix,
        const cv::Mat& distortion,
        const cv::Size& imageSize)
    {
        std::vector<cv::Point2f> samples;
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                samples.emplace_back(
                    column * static_cast<float>(imageSize.width - 1) / 2.0F,
                    row * static_cast<float>(imageSize.height - 1) / 2.0F);
            }
        }
        std::vector<cv::Point2f> corrected;
        cv::undistortPoints(samples, corrected, cameraMatrix, distortion,
            cv::noArray(), cameraMatrix);
        double maximumShift = 0.0;
        for (std::size_t index = 0; index < samples.size(); ++index)
            maximumShift = std::max(maximumShift,
                static_cast<double>(cv::norm(samples[index] - corrected[index])));
        return maximumShift;
    }

    bool calibrateAndSave(
        const std::vector<CapturedView>& views,
        const cv::Size& imageSize,
        const Options& options)
    {
        if (views.size() < MIN_CALIBRATION_VIEWS)
        {
            std::cerr << "Need at least " << MIN_CALIBRATION_VIEWS
                << " different views; currently " << views.size() << ".\n";
            return false;
        }

        std::string diversityError;
        if (!captureSetIsDiverse(views, imageSize, diversityError))
        {
            std::cerr << "Calibration not saved: " << diversityError << ".\n";
            return false;
        }

        std::vector<std::vector<cv::Point3f>> objectPoints;
        std::vector<std::vector<cv::Point2f>> imagePoints;
        objectPoints.reserve(views.size());
        imagePoints.reserve(views.size());
        for (const CapturedView& view : views)
        {
            objectPoints.push_back(view.objectPoints);
            imagePoints.push_back(view.imagePoints);
        }

        const double initialFocalLength =
            static_cast<double>(std::max(imageSize.width, imageSize.height));
        cv::Mat cameraMatrix = (cv::Mat_<double>(3, 3) <<
            initialFocalLength, 0.0, (imageSize.width - 1) * 0.5,
            0.0, initialFocalLength, (imageSize.height - 1) * 0.5,
            0.0, 0.0, 1.0);
        cv::Mat distortion = cv::Mat::zeros(1, 5, CV_64F);
        std::vector<cv::Mat> rotations;
        std::vector<cv::Mat> translations;
        const int calibrationFlags = cv::CALIB_USE_INTRINSIC_GUESS |
            cv::CALIB_FIX_ASPECT_RATIO | cv::CALIB_FIX_K3;
        const double rms = cv::calibrateCamera(
            objectPoints, imagePoints, imageSize, cameraMatrix, distortion,
            rotations, translations, calibrationFlags);
        if (!cv::checkRange(cameraMatrix) || !cv::checkRange(distortion))
        {
            std::cerr << "Calibration returned non-finite values. Capture more diverse views.\n";
            return false;
        }

        const std::vector<double> errors = perViewErrors(
            views, cameraMatrix, distortion, rotations, translations);
        const double maximumError = errors.empty() ? 0.0 :
            *std::max_element(errors.begin(), errors.end());
        const double meanError = errors.empty() ? 0.0 :
            std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();

        const double focalX = cameraMatrix.at<double>(0, 0);
        const double focalY = cameraMatrix.at<double>(1, 1);
        const double principalX = cameraMatrix.at<double>(0, 2);
        const double principalY = cameraMatrix.at<double>(1, 2);
        const double focalLimit = std::max(imageSize.width, imageSize.height);
        const double focalRatio = focalX / focalY;
        const double maximumShift = maximumUndistortShift(
            cameraMatrix, distortion, imageSize);
        const double imageDiagonal = std::hypot(
            static_cast<double>(imageSize.width),
            static_cast<double>(imageSize.height));
        const bool plausible = std::isfinite(rms) && std::isfinite(maximumError) &&
            std::isfinite(maximumShift) && std::abs(focalRatio - 1.0) <= 0.01 &&
            focalX >= focalLimit * 0.25 && focalX <= focalLimit * 12.0 &&
            principalX >= 0.0 && principalX < imageSize.width &&
            principalY >= 0.0 && principalY < imageSize.height &&
            maximumShift <= imageDiagonal * 0.40 && rms <= 1.5 &&
            maximumError <= 2.5;
        if (!plausible)
        {
            std::cerr << std::fixed << std::setprecision(4)
                << "Calibration not saved: implausible lens solution"
                << " (fx=" << focalX << ", fy=" << focalY
                << ", cx=" << principalX << ", cy=" << principalY
                << ", maxCorrection=" << maximumShift << "px, rms=" << rms
                << "px). Add stronger board tilts and wider frame coverage.\n";
            return false;
        }

        cv::FileStorage output(options.output, cv::FileStorage::WRITE);
        if (!output.isOpened())
        {
            std::cerr << "Cannot open output file: " << options.output << '\n';
            return false;
        }
        output << "version" << 1;
        output << "channel" << options.channel;
        output << "image_width" << imageSize.width;
        output << "image_height" << imageSize.height;
        output << "dictionary" << "DICT_4X4_50";
        output << "squares_x" << BOARD_SQUARES_X;
        output << "squares_y" << BOARD_SQUARES_Y;
        output << "square_length_m" << SQUARE_LENGTH_M;
        output << "marker_length_m" << MARKER_LENGTH_M;
        output << "legacy_pattern" << static_cast<int>(options.legacyPattern);
        output << "fixed_aspect_ratio" << 1;
        output << "fixed_k3" << 1;
        output << "calibration_flags" << calibrationFlags;
        output << "camera_matrix" << cameraMatrix;
        output << "distortion_coefficients" << distortion;
        output << "maximum_undistort_shift_px" << maximumShift;
        output << "reprojection_rms_px" << rms;
        output << "mean_view_error_px" << meanError;
        output << "maximum_view_error_px" << maximumError;
        output << "captured_views" << static_cast<int>(views.size());
        output << "per_view_errors_px" << errors;
        output.release();

        std::cout << std::fixed << std::setprecision(4)
            << "Calibration saved: " << options.output << '\n'
            << "  size=" << imageSize.width << 'x' << imageSize.height
            << " views=" << views.size() << " rms=" << rms
            << "px meanView=" << meanError << "px maxView=" << maximumError
            << "px fx=fy=" << focalX << "px maxCorrection=" << maximumShift << "px\n";
        return true;
    }
}

int main(int argc, char** argv)
{
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    try
    {
        const Options options = parseOptions(argc, argv);
        const std::string windowName =
            (options.arucoOnly ? "ArUco marker probe - channel " :
                "ChArUco calibration - channel ") + std::to_string(options.channel);
        cv::VideoCapture capture = openCapture(options.source);
        if (!capture.isOpened())
        {
            std::cerr << "Failed to open input source.\n";
            return 1;
        }

#if DHD_CHARUCO_NEW_API
        const cv::aruco::Dictionary dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
        cv::aruco::CharucoBoard board(
            cv::Size(BOARD_SQUARES_X, BOARD_SQUARES_Y),
            SQUARE_LENGTH_M, MARKER_LENGTH_M, dictionary);
        board.setLegacyPattern(options.legacyPattern);
        cv::aruco::CharucoDetector detector(board);
        cv::aruco::ArucoDetector arucoDetector(dictionary);
#else
        const cv::Ptr<cv::aruco::Dictionary> dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
        const cv::Ptr<cv::aruco::CharucoBoard> board =
            cv::aruco::CharucoBoard::create(
                BOARD_SQUARES_X, BOARD_SQUARES_Y,
                SQUARE_LENGTH_M, MARKER_LENGTH_M, dictionary);
        const cv::Ptr<cv::aruco::DetectorParameters> detectorParameters =
            cv::aruco::DetectorParameters::create();
#endif

        if (!options.headless)
            cv::namedWindow(windowName, cv::WINDOW_NORMAL);

        std::vector<CapturedView> views;
        cv::Size imageSize;
        int maximumMarkers = 0;
        int maximumCorners = 0;
        std::set<int> observedMarkerIds;
        std::uint64_t frameId = 0;

        while (options.maxFrames == 0 || frameId < options.maxFrames)
        {
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty())
            {
                std::cerr << "Frame read failed at frame " << frameId << ".\n";
                break;
            }
            ++frameId;
            if (imageSize.empty()) imageSize = frame.size();
            if (frame.size() != imageSize)
            {
                std::cerr << "Stream resolution changed during calibration.\n";
                return 2;
            }

            std::vector<cv::Point2f> charucoCorners;
            std::vector<int> charucoIds;
            std::vector<std::vector<cv::Point2f>> markerCorners;
            std::vector<int> markerIds;
            if (options.arucoOnly)
#if DHD_CHARUCO_NEW_API
                arucoDetector.detectMarkers(frame, markerCorners, markerIds);
            else
                detector.detectBoard(frame, charucoCorners, charucoIds,
                    markerCorners, markerIds);
#else
                cv::aruco::detectMarkers(
                    frame, dictionary, markerCorners, markerIds,
                    detectorParameters);
            else
            {
                cv::aruco::detectMarkers(
                    frame, dictionary, markerCorners, markerIds,
                    detectorParameters);
                if (!markerIds.empty())
                    cv::aruco::interpolateCornersCharuco(
                        markerCorners, markerIds, frame, board,
                        charucoCorners, charucoIds);
            }
#endif
            observedMarkerIds.insert(markerIds.begin(), markerIds.end());
            const bool newMarkerMaximum =
                static_cast<int>(markerIds.size()) > maximumMarkers;
            const bool newCornerMaximum =
                static_cast<int>(charucoIds.size()) > maximumCorners;
            maximumMarkers = std::max(maximumMarkers,
                static_cast<int>(markerIds.size()));
            maximumCorners = std::max(maximumCorners,
                static_cast<int>(charucoIds.size()));

            if (options.probeOnly &&
                (frameId == 1 || frameId % 15 == 0 ||
                 newMarkerMaximum || newCornerMaximum))
            {
                std::cout << "frame=" << frameId
                    << " markers=" << markerIds.size();
                if (!markerIds.empty())
                {
                    std::cout << " ids=";
                    for (std::size_t index = 0; index < markerIds.size(); ++index)
                    {
                        if (index > 0) std::cout << ',';
                        std::cout << markerIds[index];
                    }
                    if (options.arucoOnly)
                    {
                        std::cout << " centers=";
                        for (std::size_t index = 0; index < markerIds.size(); ++index)
                        {
                            cv::Point2f center;
                            for (const cv::Point2f& corner : markerCorners[index])
                                center += corner;
                            center *= 0.25F;
                            if (index > 0) std::cout << ';';
                            std::cout << markerIds[index] << ':'
                                << std::fixed << std::setprecision(1)
                                << center.x << ',' << center.y;
                        }
                    }
                }
                if (!options.arucoOnly)
                    std::cout << "/17 charucoCorners=" << charucoIds.size() << "/24";
                std::cout << '\n';
            }

            if (options.headless) continue;

            cv::Mat preview = frame.clone();
            if (!markerIds.empty())
                cv::aruco::drawDetectedMarkers(preview, markerCorners, markerIds);
            if (!charucoIds.empty())
                cv::aruco::drawDetectedCornersCharuco(
                    preview, charucoCorners, charucoIds, cv::Scalar(0, 255, 0));
            std::ostringstream label;
            if (options.arucoOnly)
            {
                label << "CH" << options.channel << " ArUco markers "
                    << markerIds.size() << " IDs";
                for (int id : markerIds) label << ' ' << id;
                label << "  [Q quit]";
            }
            else
            {
                label << "CH" << options.channel << " visible markers "
                    << markerIds.size() << "/17 corners "
                    << charucoIds.size() << "/24  captured poses " << views.size()
                    << "  [SPACE capture, U undo, C finish/save, Q quit]";
            }
            cv::putText(preview, label.str(), cv::Point(12, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
            cv::putText(preview, label.str(), cv::Point(12, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            cv::imshow(windowName, preview);

            const int key = cv::waitKey(1) & 0xff;
            if (key == 27 || key == 'q' || key == 'Q') break;
            if (options.arucoOnly) continue;
            if (key == 'u' || key == 'U')
            {
                if (!views.empty()) views.pop_back();
                std::cout << "Captured views: " << views.size() << '\n';
            }
            else if (key == ' ')
            {
                bool collinear = false;
#if DHD_CHARUCO_NEW_API
                collinear = board.checkCharucoCornersCollinear(charucoIds);
#else
                collinear = cv::aruco::testCharucoCornersCollinear(
                    board, charucoIds);
#endif
                if (static_cast<int>(charucoIds.size()) < MIN_CAPTURED_CORNERS ||
                    collinear)
                {
                    std::cout << "Rejected: need at least " << MIN_CAPTURED_CORNERS
                        << " non-collinear ChArUco corners.\n";
                    continue;
                }
                CapturedView view;
#if DHD_CHARUCO_NEW_API
                board.matchImagePoints(charucoCorners, charucoIds,
                    view.objectPoints, view.imagePoints);
#else
                for (std::size_t index = 0; index < charucoIds.size(); ++index)
                {
                    const int cornerId = charucoIds[index];
                    if (cornerId < 0 ||
                        static_cast<std::size_t>(cornerId) >=
                            board->chessboardCorners.size())
                        continue;
                    view.objectPoints.push_back(board->chessboardCorners[cornerId]);
                    view.imagePoints.push_back(charucoCorners[index]);
                }
#endif
                view.charucoCorners = static_cast<int>(charucoIds.size());
                if (view.objectPoints.size() != view.imagePoints.size() ||
                    view.objectPoints.size() < MIN_CAPTURED_CORNERS)
                {
                    std::cout << "Rejected: board point matching failed.\n";
                    continue;
                }
                views.push_back(std::move(view));
                std::cout << "Captured view " << views.size() << " with "
                    << charucoIds.size() << " corners";
                if (views.size() >= RECOMMENDED_CALIBRATION_VIEWS)
                    std::cout << " (recommended count reached)";
                std::cout << ".\n";
            }
            else if (key == 'c' || key == 'C')
            {
                if (calibrateAndSave(views, imageSize, options)) break;
            }
        }

        capture.release();
        if (!options.headless) cv::destroyWindow(windowName);
        if (options.probeOnly)
        {
            std::cout << "Probe result: size=" << imageSize.width << 'x' << imageSize.height
                << " maxMarkers=" << maximumMarkers;
            if (options.arucoOnly)
            {
                std::cout << " observedIds=";
                bool first = true;
                for (int id : observedMarkerIds)
                {
                    if (!first) std::cout << ',';
                    std::cout << id;
                    first = false;
                }
                std::cout << '\n';
                return observedMarkerIds.empty() ? 3 : 0;
            }
            std::cout << "/17 maxCharucoCorners=" << maximumCorners << "/24\n";
            return maximumCorners >= MIN_CAPTURED_CORNERS ? 0 : 3;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
