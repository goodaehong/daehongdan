cmake_minimum_required(VERSION 3.16)
project(FireDetectionOptimized LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

add_executable(fire_detection
    main.cpp
    CameraStream.cpp
    ConsoleFireApplication.cpp
    FireView.cpp
    FireAlarmController.cpp
    FireCandidateTracker.cpp
    FireDetectionRuntime.cpp
    FireDetector_1.cpp
    FireMaskBuilder.cpp
)

target_include_directories(fire_detection PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fire_detection PRIVATE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(fire_detection PRIVATE /O2 /EHsc)
else()
    target_compile_options(fire_detection PRIVATE -O3 -DNDEBUG)
    target_link_libraries(fire_detection PRIVATE pthread)
endif()
