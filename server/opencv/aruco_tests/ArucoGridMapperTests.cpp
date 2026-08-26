#include "GridCoordinateMapper.h"
#include "GroundFootprintEstimator.h"
#include "DisplayRadiusSmoother.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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
    constexpr int WIDTH = 1280;
    constexpr int HEIGHT = 720;
    constexpr float MODEL_SCALE = 50.0F;

    struct TestMarker
    {
        int id;
        cv::Point2f center;
        float size;
    };

    const std::vector<TestMarker> MARKERS{
        {0, {31.50F, 1.50F}, 0.04F},
        {1, {58.50F, 1.50F}, 0.04F},
        {2, {31.50F, 28.50F}, 0.04F},
        {3, {45.00F, 14.00F}, 0.04F},
        // The configuration intentionally registers ID 4 at a different
        // world centre.  Centre RANSAC must reject it and retain IDs 0..3.
        {4, {52.00F, 22.00F}, 0.04F}
    };

    void expect(bool condition, const std::string& message)
    {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void testDisplayRadiusSmoothing()
    {
        DisplayRadiusSmoother smoother;
        expect(smoother.update(1) == 1, "radius initializes from the first estimate");

        // Alternating one-cell jitter must not make the published radius flicker.
        const int jitter[] = {2, 1, 2, 1, 2, 1, 2, 1};
        for (const int radius : jitter)
            expect(smoother.update(radius) == 1,
                "alternating radius jitter must keep the stable value");

        // A sustained size change must still propagate after confirmation.
        bool changed = false;
        for (int i = 0; i < 8; ++i)
            changed = changed || smoother.update(3) == 3;
        expect(changed, "sustained radius change must eventually be published");

        smoother.reset();
        expect(smoother.update(2) == 2, "reset must discard the previous radius");
    }

    std::vector<cv::Point2f> markerWorldCorners(const TestMarker& marker)
    {
        const float half = marker.size * MODEL_SCALE * 0.5F;
        return {
            {marker.center.x - half, marker.center.y - half},
            {marker.center.x + half, marker.center.y - half},
            {marker.center.x + half, marker.center.y + half},
            {marker.center.x - half, marker.center.y + half}
        };
    }

    cv::Mat worldToImageHomography(const std::vector<cv::Point2f>& imageBoardCorners)
    {
        const std::vector<cv::Point2f> worldBoardCorners{
            {30.00F, 0.00F}, {60.00F, 0.00F},
            {60.00F, 30.00F}, {30.00F, 30.00F}
        };
        return cv::getPerspectiveTransform(worldBoardCorners, imageBoardCorners);
    }

    void drawMarker(
        cv::Mat& frame,
        const TestMarker& marker,
        const cv::Mat& worldToImage,
#if DHD_ARUCO_NEW_API
        const cv::aruco::Dictionary& dictionary)
#else
        const cv::Ptr<cv::aruco::Dictionary>& dictionary)
#endif
    {
        constexpr int MARKER_PIXELS = 360;
        cv::Mat markerImage;
#if DHD_ARUCO_NEW_API
        dictionary.generateImageMarker(marker.id, MARKER_PIXELS, markerImage, 1);
#else
        cv::aruco::drawMarker(
            dictionary, marker.id, MARKER_PIXELS, markerImage, 1);
#endif
        std::vector<cv::Point2f> imageCorners;
        cv::perspectiveTransform(
            markerWorldCorners(marker), imageCorners, worldToImage);
        const std::vector<cv::Point2f> sourceCorners{
            {0.0F, 0.0F},
            {static_cast<float>(MARKER_PIXELS - 1), 0.0F},
            {static_cast<float>(MARKER_PIXELS - 1), static_cast<float>(MARKER_PIXELS - 1)},
            {0.0F, static_cast<float>(MARKER_PIXELS - 1)}
        };
        const cv::Mat markerToFrame = cv::getPerspectiveTransform(
            sourceCorners, imageCorners);
        cv::Mat warped(frame.size(), CV_8UC1, cv::Scalar(255));
        cv::warpPerspective(
            markerImage, warped, markerToFrame, frame.size(),
            cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(255));
        cv::Mat mask(frame.size(), CV_8UC1, cv::Scalar(0));
        cv::fillConvexPoly(
            mask, std::vector<cv::Point>{
                cv::Point(cvRound(imageCorners[0].x), cvRound(imageCorners[0].y)),
                cv::Point(cvRound(imageCorners[1].x), cvRound(imageCorners[1].y)),
                cv::Point(cvRound(imageCorners[2].x), cvRound(imageCorners[2].y)),
                cv::Point(cvRound(imageCorners[3].x), cvRound(imageCorners[3].y))},
            cv::Scalar(255));
        cv::Mat warpedBgr;
        cv::cvtColor(warped, warpedBgr, cv::COLOR_GRAY2BGR);
        warpedBgr.copyTo(frame, mask);
    }

    cv::Mat makeFrame(
        const std::vector<cv::Point2f>& imageBoardCorners,
        const std::vector<int>& visibleMarkerIds)
    {
        cv::Mat frame(HEIGHT, WIDTH, CV_8UC3, cv::Scalar(210, 210, 210));
        const cv::Mat homography = worldToImageHomography(imageBoardCorners);
#if DHD_ARUCO_NEW_API
        const cv::aruco::Dictionary dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
#else
        const cv::Ptr<cv::aruco::Dictionary> dictionary =
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
#endif
        for (const TestMarker& marker : MARKERS)
        {
            if (std::find(visibleMarkerIds.begin(), visibleMarkerIds.end(), marker.id) ==
                visibleMarkerIds.end())
                continue;
            drawMarker(frame, marker, homography, dictionary);
        }
        return frame;
    }

    cv::Point2f normalizedImagePoint(
        const cv::Point2f& worldPoint,
        const cv::Mat& worldToImage)
    {
        std::vector<cv::Point2f> image;
        cv::perspectiveTransform(
            std::vector<cv::Point2f>{worldPoint}, image, worldToImage);
        return {
            image[0].x / static_cast<float>(WIDTH - 1),
            image[0].y / static_cast<float>(HEIGHT - 1)
        };
    }
}

int main()
{
    testDisplayRadiusSmoothing();

    {
        const GroundFootprintEstimate footprint = estimateCircularGroundFootprint(
            cv::Point2f(10.0F, 20.0F), cv::Point2f(12.0F, 20.0F),
            cv::Point(30, 30), cv::Point(34, 30));
        expect(footprint.valid, "2 m ground footprint should be valid");
        expect(std::abs(footprint.widthMetres - 2.0) < 1e-6,
            "ground footprint width should use mapped factory metres");
        expect(std::abs(footprint.areaSquareMetres - 3.14159265358979323846) < 1e-6,
            "2 m circular footprint should have pi square metres of area");
        expect(footprint.displayRadiusCells == 2,
            "4-cell footprint diameter should produce a 2-cell display radius");

        const GroundFootprintEstimate tiny = estimateCircularGroundFootprint(
            cv::Point2f(10.0F, 20.0F), cv::Point2f(10.1F, 20.0F),
            cv::Point(30, 30), cv::Point(30, 30));
        expect(tiny.valid && tiny.displayRadiusCells == 1,
            "sub-cell fire should remain visible with a one-cell radius");

        const GroundFootprintEstimate empty = estimateCircularGroundFootprint(
            cv::Point2f(10.0F, 20.0F), cv::Point2f(10.0F, 20.0F),
            cv::Point(30, 30), cv::Point(30, 30));
        expect(!empty.valid, "zero-width footprint must be rejected");
    }

    GridCoordinateMapper mapper;
    expect(
        mapper.loadArucoBoardConfiguration(TEST_CONFIG_PATH, 3),
        "channel 4 ArUco configuration should load: " + mapper.lastError());

    const std::filesystem::path calibrationPath =
        std::filesystem::temp_directory_path() /
        "dhd_camera_calibration_ch4_test.yml";
    {
        cv::FileStorage output(calibrationPath.string(), cv::FileStorage::WRITE);
        output << "version" << 1;
        output << "channel" << 4;
        output << "image_width" << WIDTH;
        output << "image_height" << HEIGHT;
        output << "camera_matrix" <<
            (cv::Mat_<double>(3, 3) <<
                900.0, 0.0, WIDTH * 0.5,
                0.0, 900.0, HEIGHT * 0.5,
                0.0, 0.0, 1.0);
        output << "distortion_coefficients" <<
            (cv::Mat_<double>(1, 5) << 0.0, 0.0, 0.0, 0.0, 0.0);
        output << "reprojection_rms_px" << 0.4;
    }
    expect(mapper.loadCameraCalibration(calibrationPath.string(), 3),
        "channel 4 camera calibration should load: " +
        mapper.cameraCalibrationError());
    GridCoordinateMapper wrongChannelMapper;
    expect(!wrongChannelMapper.loadCameraCalibration(calibrationPath.string(), 2),
        "camera calibration must reject a different logical channel");

    const std::filesystem::path implausibleCalibrationPath =
        std::filesystem::temp_directory_path() /
        "dhd_camera_calibration_ch4_implausible_test.yml";
    {
        cv::FileStorage output(
            implausibleCalibrationPath.string(), cv::FileStorage::WRITE);
        output << "version" << 1;
        output << "channel" << 4;
        output << "image_width" << WIDTH;
        output << "image_height" << HEIGHT;
        output << "camera_matrix" <<
            (cv::Mat_<double>(3, 3) <<
                900.0, 0.0, WIDTH * 0.5,
                0.0, 2200.0, HEIGHT * 0.5,
                0.0, 0.0, 1.0);
        output << "distortion_coefficients" <<
            (cv::Mat_<double>(1, 5) << -1.0, -2.0, 0.0, 0.0, 20.0);
        output << "reprojection_rms_px" << 0.25;
    }
    GridCoordinateMapper implausibleMapper;
    expect(!implausibleMapper.loadCameraCalibration(
        implausibleCalibrationPath.string(), 3),
        "low RMS must not allow an implausible lens calibration");

    // The camera sees three board corners plus the centre marker. The fourth
    // board corner near the camera is outside the image and has no marker.
    const std::vector<int> visible{0, 1, 2, 3};
    const std::vector<cv::Point2f> firstBoardImage{
        {260.0F, 95.0F}, {1050.0F, 125.0F},
        {1145.0F, 650.0F}, {145.0F, 610.0F}
    };
    const cv::Mat firstWorldToImage = worldToImageHomography(firstBoardImage);
    const cv::Mat firstFrame = makeFrame(firstBoardImage, visible);
    const bool firstFrameUpdated = mapper.updateFromFrame(firstFrame, 1);
    if (!firstFrameUpdated)
        std::cerr << "first frame status: " << mapper.status().message << '\n';
    expect(firstFrameUpdated,
        "three corners plus centre should update Homography: " + mapper.lastError());
    const ArucoMappingStatus firstStatus = mapper.status();
    expect(firstStatus.acceptedMarkers >= 4, "four configured markers should be accepted");
    expect(firstStatus.inlierCorners == 4,
        "centre Homography should report four inlier marker centres");
    expect(firstStatus.lensCalibrationConfigured &&
        firstStatus.lensCalibrationApplied,
        "loaded lens calibration should be applied to marker and fire points");

    cv::Point grid;
    cv::Point2f factoryPoint;
    expect(mapper.map(
        normalizedImagePoint({45.00F, 15.00F}, firstWorldToImage),
        grid, &factoryPoint),
        "channel-4 centre should map to the full-factory display grid");
    expect(std::abs(grid.x - 44) <= 1 && std::abs(grid.y - 44) <= 1,
        "channel-4 centre should map near full-factory grid (44,44); "
        "grid Y is flipped from world Y since the display's row 0 is the top edge");
    expect(std::abs(factoryPoint.x - 45.0F) < 0.1F &&
        std::abs(factoryPoint.y - 15.0F) < 0.1F,
        "mapper should expose actual factory coordinates in metres");

    GridCoordinateMapper outlierMapper;
    expect(outlierMapper.loadArucoBoardConfiguration(TEST_CONFIG_PATH, 3),
        "legacy QUALITY 4 12 configuration should migrate to four centre inliers");
    expect(outlierMapper.loadCameraCalibration(calibrationPath.string(), 3),
        "outlier mapper camera calibration should load");
    const cv::Mat oneBadCentreFrame = makeFrame(
        firstBoardImage, {0, 1, 2, 3, 4});
    expect(outlierMapper.updateFromFrame(oneBadCentreFrame, 1),
        "one rejected marker centre must leave a valid four-centre Homography: " +
        outlierMapper.lastError());
    const ArucoMappingStatus outlierStatus = outlierMapper.status();
    expect(outlierStatus.inlierCorners == 4,
        "RANSAC should retain exactly four marker centres");
    expect(outlierStatus.message.find("rejectedIds=[4]") != std::string::npos,
        "success log should identify the rejected marker ID");

    const std::filesystem::path staticHomographyPath =
        std::filesystem::temp_directory_path() /
        "dhd_static_homography_ch4_test.yml";
    expect(mapper.saveStaticHomography(staticHomographyPath.string()),
        "accepted ArUco mapping should save a static Homography: " +
        mapper.lastError());
    GridCoordinateMapper staticMapper;
    expect(staticMapper.loadArucoBoardConfiguration(TEST_CONFIG_PATH, 3),
        "static mapper configuration should load");
    expect(staticMapper.loadCameraCalibration(calibrationPath.string(), 3),
        "static mapper camera calibration should load");
    expect(staticMapper.loadStaticHomography(staticHomographyPath.string(), 3),
        "saved static Homography should reload: " + staticMapper.lastError());
    expect(staticMapper.isReady() && staticMapper.hasStaticHomography() &&
        staticMapper.status().staticHomography,
        "loaded static Homography should remain ready without markers");
    expect(staticMapper.map(
        normalizedImagePoint({45.00F, 15.00F}, firstWorldToImage),
        grid, &factoryPoint),
        "loaded static Homography should map the original point");
    staticMapper.resetTracking();
    expect(staticMapper.isReady(),
        "stream reset must retain a loaded static Homography");
    expect(!staticMapper.updateFromFrame(firstFrame, 2),
        "production static mode must skip per-frame ArUco updates");

    // Simulate camera shake and require a new Homography from the same fixed board.
    const std::vector<cv::Point2f> secondBoardImage{
        {285.0F, 120.0F}, {1075.0F, 100.0F},
        {1110.0F, 630.0F}, {175.0F, 645.0F}
    };
    const cv::Mat secondWorldToImage = worldToImageHomography(secondBoardImage);
    const cv::Mat secondFrame = makeFrame(secondBoardImage, visible);
    const bool shiftedFrameUpdated = mapper.updateFromFrame(secondFrame, 2);
    expect(shiftedFrameUpdated,
        "camera-shifted frame should refresh Homography: " + mapper.lastError());
    expect(mapper.map(normalizedImagePoint({37.50F, 22.50F}, secondWorldToImage), grid),
        "point after camera shake should still map");
    expect(std::abs(grid.x - 37) <= 2 && std::abs(grid.y - 37) <= 2,
        "camera shake corrected grid coordinate should remain stable");

    mapper.resetTracking();
    const cv::Mat insufficientFrame = makeFrame(firstBoardImage, {0});
    expect(!mapper.updateFromFrame(insufficientFrame, 3),
        "one marker must not refresh a full-board Homography");
    expect(!mapper.isReady(), "mapper should remain invalid after reset with one marker");

    std::error_code removeError;
    std::filesystem::remove(calibrationPath, removeError);
    std::filesystem::remove(implausibleCalibrationPath, removeError);
    std::filesystem::remove(staticHomographyPath, removeError);

    std::cout << "ArucoGridMapper tests passed\n";
    return 0;
}
