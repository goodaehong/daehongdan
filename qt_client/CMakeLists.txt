cmake_minimum_required(VERSION 3.19)
project(qt_dashboard LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(qt_dashboard
    WIN32 MACOSX_BUNDLE
    dashboard_main.cpp
    src/core/MainWindow.cpp
    src/core/MainWindow.h
    src/core/ZoneTypes.cpp
    src/core/ZoneTypes.h
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
    src/widgets/StatusPanel.cpp
    src/widgets/StatusPanel.h
    src/widgets/GasGraphWidget.cpp
    src/widgets/GasGraphWidget.h
    src/network/StreamReceiver.cpp
    src/network/StreamReceiver.h
)

target_include_directories(qt_dashboard
    PRIVATE
        src
)

target_link_libraries(qt_dashboard
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
        Qt::MultimediaWidgets
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
