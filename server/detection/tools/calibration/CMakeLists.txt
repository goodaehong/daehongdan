cmake_minimum_required(VERSION 3.16)
project(CharucoCameraCalibrator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS
    core imgproc calib3d objdetect highgui videoio)

add_executable(charuco_calibrator CharucoCalibrator.cpp)
target_include_directories(charuco_calibrator PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(charuco_calibrator PRIVATE ${OpenCV_LIBS})

add_executable(fixed_homography_calibrator
    FixedHomographyCalibrator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GridCoordinateMapper.cpp)
target_include_directories(fixed_homography_calibrator PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(fixed_homography_calibrator PRIVATE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(charuco_calibrator PRIVATE /O2 /EHsc /utf-8)
    target_compile_options(fixed_homography_calibrator PRIVATE /O2 /EHsc /utf-8)
else()
    target_compile_options(charuco_calibrator PRIVATE -O3)
    target_compile_options(fixed_homography_calibrator PRIVATE -O3)
endif()
