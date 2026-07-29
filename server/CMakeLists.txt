cmake_minimum_required(VERSION 3.16)
project(server)

if(NOT CMAKE_BUILD_TYPE)          # ★ 추가
    set(CMAKE_BUILD_TYPE Release)  # ★ 최적화(-O3) 켜기
endif()                           # ★

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED) 

# 재환님 화재 감지 코어 (server/opencv). 하네스(CameraStream/main 등)는 제외
set(FIRE_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/opencv)
set(FIRE_CORE_SRC
    ${FIRE_CORE_DIR}/FlameDetector.cpp
    ${FIRE_CORE_DIR}/FireAlarmController.cpp
    ${FIRE_CORE_DIR}/FireDetectionRuntime.cpp
)
# DB 코어 (server/db)                                          
set(DB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/db)
set(DB_SRC ${DB_DIR}/Database.cpp)

add_executable(server_main server_main.cpp ${FIRE_CORE_SRC} ${DB_SRC})
target_include_directories(server_main PRIVATE ${OpenCV_INCLUDE_DIRS} ${FIRE_CORE_DIR} ${DB_DIR} ${SQLite3_INCLUDE_DIRS})
target_link_libraries(server_main PRIVATE ${OpenCV_LIBS} Threads::Threads ${SQLite3_LIBRARIES})
target_compile_definitions(server_main PRIVATE DB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/db/factory.db")