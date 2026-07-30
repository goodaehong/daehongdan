cmake_minimum_required(VERSION 3.16)
project(server)

if(NOT CMAKE_BUILD_TYPE)          # ★ 추가
    set(CMAKE_BUILD_TYPE Release)  # ★ 최적화(-O3) 켜기
endif()                           # ★

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED) 

# 연기 감지(NCNN) 스위치                                        
option(ENABLE_SMOKE_NCNN "Enable smoke detection with Tencent NCNN" ON)
if(ENABLE_SMOKE_NCNN)
    find_package(ncnn CONFIG QUIET)
endif()        

# 재환님 화재 감지 코어 (server/opencv). 하네스(CameraStream/main 등)는 제외
set(FIRE_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/opencv)
set(FIRE_CORE_SRC
    ${FIRE_CORE_DIR}/FlameDetector.cpp
    ${FIRE_CORE_DIR}/FireAlarmController.cpp
    ${FIRE_CORE_DIR}/FireDetectionRuntime.cpp
    ${FIRE_CORE_DIR}/SmokeDetector.cpp                       
    ${FIRE_CORE_DIR}/SmokeDetectionRuntime.cpp  
    ${FIRE_CORE_DIR}/PersonMetadataReceiver.cpp
)
# DB 코어 (server/db)                                          
set(DB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/db)
set(DB_SRC ${DB_DIR}/Database.cpp)

add_executable(server_main server_main.cpp ${FIRE_CORE_SRC} ${DB_SRC})
target_include_directories(server_main PRIVATE ${OpenCV_INCLUDE_DIRS} ${FIRE_CORE_DIR} ${DB_DIR} ${SQLite3_INCLUDE_DIRS})
target_link_libraries(server_main PRIVATE ${OpenCV_LIBS} Threads::Threads ${SQLite3_LIBRARIES})
target_compile_definitions(server_main PRIVATE DB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/db/factory.db")

# NCNN 연결 (연기 감지)                                         
if(ENABLE_SMOKE_NCNN)
    if(NOT ncnn_FOUND)
        message(FATAL_ERROR "ENABLE_SMOKE_NCNN=ON 인데 NCNN 못 찾음. -Dncnn_DIR=/usr/local/lib/cmake/ncnn 지정")
    endif()
    target_compile_definitions(server_main PRIVATE SMOKE_HAS_NCNN=1)
    target_link_libraries(server_main PRIVATE ncnn)
    message(STATUS "Smoke detection: NCNN enabled")
else()
    target_compile_definitions(server_main PRIVATE SMOKE_HAS_NCNN=0)
endif()

# 모델 파일을 실행파일 옆에 복사 (AppConfig 모델 경로가 상대경로라)   
add_custom_command(TARGET server_main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${FIRE_CORE_DIR}/models"
        "$<TARGET_FILE_DIR:server_main>/models"
    COMMENT "연기 NCNN 모델 복사")                              