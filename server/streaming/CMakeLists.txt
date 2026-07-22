cmake_minimum_required(VERSION 3.16)
project(streaming)

if(NOT CMAKE_BUILD_TYPE)          # ★ 추가
    set(CMAKE_BUILD_TYPE Release)  # ★ 최적화(-O3) 켜기
endif()                           # ★

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# 재환님 화재 감지 코어 (server/opencv). 하네스(CameraStream/main 등)는 제외
set(FIRE_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../opencv)
set(FIRE_CORE_SRC
    ${FIRE_CORE_DIR}/FlameDetector.cpp
    ${FIRE_CORE_DIR}/FireAlarmController.cpp
    ${FIRE_CORE_DIR}/FireDetectionRuntime.cpp
)

add_executable(streaming stream_receiver.cpp ${FIRE_CORE_SRC})
target_include_directories(streaming PRIVATE ${OpenCV_INCLUDE_DIRS} ${FIRE_CORE_DIR})
target_link_libraries(streaming PRIVATE ${OpenCV_LIBS} Threads::Threads)