cmake_minimum_required(VERSION 3.16)
project(DetectionDebugRunner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui calib3d objdetect)

add_executable(DetectionDebugRunner
    DetectionDebugRunner.cpp
    ../FlameDetector.cpp
    ../FireAlarmController.cpp
    ../FireDetectionRuntime.cpp
    ../GridCoordinateMapper.cpp
    ../IgnoreRegionFilter.cpp
    ../SmokeDetector.cpp
    ../SmokeDetectionRuntime.cpp)

target_include_directories(DetectionDebugRunner PRIVATE .. ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(DetectionDebugRunner PRIVATE SMOKE_HAS_NCNN=0)
target_link_libraries(DetectionDebugRunner PRIVATE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(DetectionDebugRunner PRIVATE /O2 /EHsc /utf-8)
else()
    target_compile_options(DetectionDebugRunner PRIVATE -O3)
    target_link_libraries(DetectionDebugRunner PRIVATE pthread)
endif()
