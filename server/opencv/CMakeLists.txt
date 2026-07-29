cmake_minimum_required(VERSION 3.16)
project(FireDetectionOptimized LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

option(ENABLE_SMOKE_NCNN "Enable smoke detection with Tencent NCNN" ON)
if(ENABLE_SMOKE_NCNN)
    find_package(ncnn CONFIG QUIET)
endif()

add_executable(fire_detection
    main.cpp
    CameraStream.cpp
    ConsoleFireApplication.cpp
    FireView.cpp
    FireAlarmController.cpp
    FireDetectionRuntime.cpp
    FlameDetector.cpp
    PersonMetadataReceiver.cpp
    SmokeDetector.cpp
    SmokeDetectionRuntime.cpp
)

target_include_directories(fire_detection PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fire_detection PRIVATE ${OpenCV_LIBS})

if(ENABLE_SMOKE_NCNN)
    if(NOT ncnn_FOUND)
        message(FATAL_ERROR
            "ENABLE_SMOKE_NCNN=ON but NCNN was not found. "
            "Set ncnn_DIR or CMAKE_PREFIX_PATH to ncnnConfig.cmake.")
    endif()
    target_compile_definitions(fire_detection PRIVATE SMOKE_HAS_NCNN=1)
    target_link_libraries(fire_detection PRIVATE ncnn)
    message(STATUS "Smoke detection: NCNN enabled")
else()
    target_compile_definitions(fire_detection PRIVATE SMOKE_HAS_NCNN=0)
    message(STATUS "Smoke detection: explicitly disabled")
endif()

add_custom_command(TARGET fire_detection POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:fire_detection>/models"
    COMMENT "Copying smoke NCNN model beside the executable"
    VERBATIM)

if(MSVC)
    target_compile_options(fire_detection PRIVATE /O2 /EHsc)
else()
    target_compile_options(fire_detection PRIVATE -O3 -DNDEBUG)
    target_link_libraries(fire_detection PRIVATE pthread)
endif()
