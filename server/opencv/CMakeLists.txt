cmake_minimum_required(VERSION 3.19)
project(fire_bridge_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "C:/opencv/build")
endif()
find_package(OpenCV REQUIRED)

add_executable(fire_bridge_server
    BridgeMain.cpp
    AppConfig.h
    DetectionTypes.h
    CameraStream.cpp
    CameraStream.h
    FireMaskBuilder.cpp
    FireMaskBuilder.h
    FireCandidateTracker.cpp
    FireCandidateTracker.h
    FireDetector_1.cpp
    FireDetector_1.h
    FireAlarmController.cpp
    FireAlarmController.h
    FireDetectionRuntime.cpp
    FireDetectionRuntime.h
)

target_include_directories(fire_bridge_server PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fire_bridge_server PRIVATE ${OpenCV_LIBS} ws2_32)
