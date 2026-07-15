cmake_minimum_required(VERSION 3.19)
project(qt_dashboard LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(qt_dashboard
    WIN32 MACOSX_BUNDLE
    dashboard_main.cpp
    src/pages/mainwindow.cpp
    src/pages/mainwindow.h
    src/pages/loginwindow.cpp
    src/pages/loginwindow.h
    src/widgets/gasgraphwidget.cpp
    src/widgets/gasgraphwidget.h
)

target_include_directories(qt_dashboard
    PRIVATE
        src/pages
        src/widgets
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
