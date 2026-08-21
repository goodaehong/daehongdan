cmake_minimum_required(VERSION 3.16)
project(server)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)   # 최적화(-O3) 켜기
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # inline constexpr 등 C++17 필수

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# 연기 감지(NCNN) 스위치
option(ENABLE_SMOKE_NCNN "Enable smoke detection with Tencent NCNN" ON)
if(ENABLE_SMOKE_NCNN)
    find_package(ncnn CONFIG QUIET)
endif()

# ── 소스 목록 ──

# 재환님 감지 코어 (server/opencv). 하네스(CameraStream/main 등)는 제외
set(FIRE_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/opencv)
set(FIRE_CORE_SRC
    ${FIRE_CORE_DIR}/IgnoreRegionFilter.cpp
    ${FIRE_CORE_DIR}/FlameDetector.cpp
    ${FIRE_CORE_DIR}/FireAlarmController.cpp
    ${FIRE_CORE_DIR}/FireDetectionRuntime.cpp
    ${FIRE_CORE_DIR}/GridCoordinateMapper.cpp
    ${FIRE_CORE_DIR}/SmokeDetector.cpp
    ${FIRE_CORE_DIR}/SmokeDetectionRuntime.cpp
    ${FIRE_CORE_DIR}/PersonMetadataReceiver.cpp
)

# DB 코어 (server/db)
set(DB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/db)
set(DB_SRC ${DB_DIR}/Database.cpp
           ${DB_DIR}/query_handler.cpp)   

# 대피경로 계산 (server/evac_map_tools). 데모용 server.cpp 는 main()이 있어 제외  
set(EVAC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/evac_map_tools)
set(EVAC_SRC ${EVAC_DIR}/EvacPlanner.cpp) 

# 서버 모듈 (대홍)
set(SERVER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/judgement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/alarm_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/qt_link.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/roi/roi_store.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/floormap/floormap_store.cpp        
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/speaker_alert.cpp
)

# ── 하드웨어 계층: mock / 실물 교체 ──
option(USE_MOCK_SENSOR   "센서 mock 사용"       OFF)
option(USE_MOCK_ACTUATOR "액추에이터 mock 사용"  OFF)
option(USE_MOCK_DISPLAY  "전광판 mock 사용"     OFF)
option(ENABLE_TLS        "Qt 연결에 TLS 사용"   OFF)
# USB 사운드카드는 꽂는 순서에 따라 hw 번호가 바뀔 수 있어 하드코딩 대신 빌드 옵션으로 뺌.
# 장치가 바뀌면 -DSPEAKER_ALSA_DEVICE=hw:X,0 으로 재지정.
set(SPEAKER_ALSA_DEVICE "hw:2,0" CACHE STRING "스피커 출력에 쓸 ALSA 장치")

if(USE_MOCK_SENSOR)
    set(SENSOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sensors/sensor_reader_mock.cpp)
else()
    set(SENSOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sensors/sensor_reader_hw.cpp
                   ${CMAKE_CURRENT_SOURCE_DIR}/sensors/sensor_conversion.cpp)
endif()

if(USE_MOCK_ACTUATOR)
    set(ACTUATOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/actuator/actuator_control_mock.cpp)
else()
    set(ACTUATOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/actuator/actuator_controller.cpp)
endif()

# 광렬님 STM32 전광판 UART 프로토콜 (drivers/stm_uart_display) - 실물 전광판 구현이 사용
set(STM_DISPLAY_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/stm_uart_display)

if(USE_MOCK_DISPLAY)
    set(DISPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/display/stm_display_mock.cpp)
else()
    set(DISPLAY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/display/stm_display.cpp
                    ${STM_DISPLAY_PROTOCOL_DIR}/stm_display_protocol.c)
endif()

if(ENABLE_TLS)
    set(NET_SRC ${CMAKE_CURRENT_SOURCE_DIR}/net/link_tls.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/net/tls_server.cpp)
else()
    set(NET_SRC ${CMAKE_CURRENT_SOURCE_DIR}/net/link_plain.cpp)
endif()

# ── 빌드 ──
add_executable(server_main
    server_main.cpp
    ${SERVER_SRC} ${SENSOR_SRC} ${ACTUATOR_SRC} ${DISPLAY_SRC} ${NET_SRC}
    ${FIRE_CORE_SRC} ${DB_SRC} ${EVAC_SRC}) 

target_include_directories(server_main PRIVATE
    ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}
    ${FIRE_CORE_DIR} ${DB_DIR} ${SQLite3_INCLUDE_DIRS} ${STM_DISPLAY_PROTOCOL_DIR}
    ${EVAC_DIR})  

target_link_libraries(server_main PRIVATE
    ${OpenCV_LIBS} Threads::Threads ${SQLite3_LIBRARIES})

target_compile_definitions(server_main PRIVATE
    DB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/db/factory.db"
    ROI_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/roi/roi_config.json" 
    FLOORMAP_IMAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/floormap/uploaded_map.png"    
    FLOORMAP_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/floormap/floor_map.json"       
    SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/db/snapshots"
    AUDIO_FILE="${CMAKE_CURRENT_SOURCE_DIR}/audio/evacuation_alert.wav"
    AUDIO_DEVICE="${SPEAKER_ALSA_DEVICE}"
    FIRE_ARUCO_CONFIG_PATH="${FIRE_CORE_DIR}/aruco_board_config.txt"
    FIRE_CAMERA_CALIBRATION_DIR="${FIRE_CORE_DIR}"
    FIRE_STATIC_HOMOGRAPHY_DIR="${FIRE_CORE_DIR}")

if(ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(server_main PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    # 인증서/개인키 절대경로 고정 (실행 CWD 종속 방지). 현장마다 다른 파일이라 git엔 안 올림(.gitignore)
    target_compile_definitions(server_main PRIVATE
        TLS_CERT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/net/server_cert.pem"
        TLS_KEY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/net/server_private.pem")
endif()

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
