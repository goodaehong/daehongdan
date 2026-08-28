# fire_uart_test 전용 빌드 파일. 이 폴더의 다른 도구(send_test_uart, sensor_uart_driver,
# test_alert_uart)는 README.md에 적힌 대로 g++ 한 줄로 빌드하던 걸 그대로 유지함 -
# 이 CMakeLists.txt는 그것들과 무관하고, OpenCV/ArUco 의존성이 있는 fire_uart_test만 빌드함.
#
# 빌드:
#   cd drivers/stm_uart_display
#   cmake -B build && cmake --build build
# 결과물: build/fire_uart_test (또는 build/Release/fire_uart_test.exe, Windows)

cmake_minimum_required(VERSION 3.16)
project(fire_uart_test LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio calib3d objdetect)

set(OPENCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../server/detection)

add_executable(fire_uart_test
    fire_uart_test.cpp
    stm_display_protocol.c
    ${OPENCV_DIR}/FlameDetector.cpp
    ${OPENCV_DIR}/FireAlarmController.cpp
    ${OPENCV_DIR}/FireDetectionRuntime.cpp
    ${OPENCV_DIR}/GridCoordinateMapper.cpp
    ${OPENCV_DIR}/IgnoreRegionFilter.cpp)

target_include_directories(fire_uart_test PRIVATE ${OPENCV_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fire_uart_test PRIVATE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(fire_uart_test PRIVATE /O2 /EHsc /utf-8)
else()
    target_compile_options(fire_uart_test PRIVATE -O3)
    target_link_libraries(fire_uart_test PRIVATE pthread)
endif()
