cmake_minimum_required(VERSION 3.19)
project(qt_dashboard LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network)

qt_standard_project_setup()

# --- libvlc (RTSP 저지연 재생용) ---------------------------------------------
# 공식 SDK 없이, 설치된 libvlc.dll에서 필요한 함수만 뽑아 MinGW 임포트 라이브러리를
# 빌드 시점에 직접 생성한다 (dlltool). VLC 설치 경로가 다르면 -DVLC_DIR=... 로 지정.
if(NOT DEFINED VLC_DIR)
    set(VLC_DIR "C:/Program Files/VideoLAN/VLC")
endif()

set(VLC_DLL "${VLC_DIR}/libvlc.dll")
if(NOT EXISTS "${VLC_DLL}")
    message(FATAL_ERROR "libvlc.dll을 찾을 수 없습니다: ${VLC_DLL} (VLC 설치 경로를 -DVLC_DIR=... 로 지정하세요)")
endif()

set(VLC_DEF "${CMAKE_CURRENT_BINARY_DIR}/libvlc.def")
set(VLC_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/libvlc.a")

file(WRITE "${VLC_DEF}"
"LIBRARY libvlc.dll\nEXPORTS\nlibvlc_new\nlibvlc_release\nlibvlc_media_new_location\nlibvlc_media_release\nlibvlc_media_add_option\nlibvlc_media_player_new_from_media\nlibvlc_media_player_release\nlibvlc_media_player_set_hwnd\nlibvlc_media_player_play\nlibvlc_media_player_stop\n"
)

get_filename_component(_mingw_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(DLLTOOL_EXECUTABLE dlltool HINTS "${_mingw_bin_dir}")
if(NOT DLLTOOL_EXECUTABLE)
    message(FATAL_ERROR "dlltool을 찾을 수 없습니다 (MinGW binutils 필요)")
endif()

add_custom_command(
    OUTPUT "${VLC_IMPLIB}"
    COMMAND "${DLLTOOL_EXECUTABLE}" -d "${VLC_DEF}" -D libvlc.dll -l "${VLC_IMPLIB}"
    DEPENDS "${VLC_DEF}"
    COMMENT "Generating MinGW import library for libvlc.dll"
)
add_custom_target(libvlc_implib DEPENDS "${VLC_IMPLIB}")

qt_add_executable(qt_dashboard
    WIN32 MACOSX_BUNDLE
    dashboard_main.cpp
    src/core/MainWindow.cpp
    src/core/MainWindow.h
    src/core/ZoneTypes.cpp
    src/core/ZoneTypes.h
    src/core/DetectionTypes.h
    src/pages/LoginPage.cpp
    src/pages/LoginPage.h
    src/pages/MonitorPage.cpp
    src/pages/MonitorPage.h
    src/pages/EventLogPage.cpp
    src/pages/EventLogPage.h
    src/pages/GraphPage.cpp
    src/pages/GraphPage.h
    src/pages/ControlPage.cpp
    src/pages/ControlPage.h
    src/pages/HelpPage.cpp
    src/pages/HelpPage.h
    src/widgets/VideoWidget.cpp
    src/widgets/VideoWidget.h
    src/widgets/DetectionOverlay.cpp
    src/widgets/DetectionOverlay.h
    src/widgets/StatusPanel.cpp
    src/widgets/StatusPanel.h
    src/widgets/GasGraphWidget.cpp
    src/widgets/GasGraphWidget.h
    src/network/StreamReceiver.cpp
    src/network/StreamReceiver.h
    src/network/ServerLink.cpp
    src/network/ServerLink.h
)

add_dependencies(qt_dashboard libvlc_implib)

target_include_directories(qt_dashboard
    PRIVATE
        src
)

target_compile_definitions(qt_dashboard
    PRIVATE
        VLC_PLUGIN_PATH="${VLC_DIR}/plugins"
)

target_link_libraries(qt_dashboard
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
        Qt::MultimediaWidgets
        Qt::Network
        "${VLC_IMPLIB}"
)

# 실행에 필요한 libvlc 런타임 dll을 실행파일 옆에 복사 (plugins 폴더는 용량이 커서
# VLC_PLUGIN_PATH 환경변수로 설치 경로를 그대로 가리킴 - dashboard_main.cpp 참고).
add_custom_command(TARGET qt_dashboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VLC_DIR}/libvlc.dll" "${VLC_DIR}/libvlccore.dll"
        "$<TARGET_FILE_DIR:qt_dashboard>"
)

install(TARGETS qt_dashboard
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET qt_dashboard
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
