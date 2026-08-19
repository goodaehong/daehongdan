cmake_minimum_required(VERSION 3.16)
project(ArucoGridMapperTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d objdetect)

add_executable(aruco_grid_mapper_tests
    ArucoGridMapperTests.cpp
    ../GridCoordinateMapper.cpp
)
target_include_directories(aruco_grid_mapper_tests PRIVATE
    ..
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(aruco_grid_mapper_tests PRIVATE ${OpenCV_LIBS})
target_compile_definitions(aruco_grid_mapper_tests PRIVATE
    TEST_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/aruco_test_config.txt"
)

enable_testing()
add_test(NAME aruco_grid_mapper_tests COMMAND aruco_grid_mapper_tests)

# Compile-only integration guard. This catches API drift between the dynamic
# mapper and the existing fire runtime without requiring server SQLite/NCNN.
add_library(aruco_fire_runtime_compile OBJECT
    ../FireDetectionRuntime.cpp
)
target_include_directories(aruco_fire_runtime_compile PRIVATE
    ..
    ${OpenCV_INCLUDE_DIRS}
)
target_compile_definitions(aruco_fire_runtime_compile PRIVATE SMOKE_HAS_NCNN=0)
