cmake_minimum_required(VERSION 3.16)
project(DetectionDebugRunner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio highgui calib3d objdetect)
option(ENABLE_SMOKE_NCNN "Enable smoke detection with Tencent NCNN" ON)
if(ENABLE_SMOKE_NCNN)
    find_package(ncnn CONFIG REQUIRED)
endif()

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
target_link_libraries(DetectionDebugRunner PRIVATE ${OpenCV_LIBS})

if(ENABLE_SMOKE_NCNN)
    target_compile_definitions(DetectionDebugRunner PRIVATE SMOKE_HAS_NCNN=1)
    target_link_libraries(DetectionDebugRunner PRIVATE ncnn)
else()
    target_compile_definitions(DetectionDebugRunner PRIVATE SMOKE_HAS_NCNN=0)
endif()

add_custom_command(TARGET DetectionDebugRunner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../models"
        "$<TARGET_FILE_DIR:DetectionDebugRunner>/models"
    COMMENT "Copying smoke NCNN model beside DetectionDebugRunner")

if(WIN32 AND ENABLE_SMOKE_NCNN)
    add_custom_command(TARGET DetectionDebugRunner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ncnn>"
            "$<TARGET_FILE_DIR:DetectionDebugRunner>"
        COMMENT "Copying ncnn runtime DLL")
endif()

if(MSVC)
    target_compile_options(DetectionDebugRunner PRIVATE /O2 /EHsc /utf-8)
else()
    target_compile_options(DetectionDebugRunner PRIVATE -O3)
    target_link_libraries(DetectionDebugRunner PRIVATE pthread)
endif()
