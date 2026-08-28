cmake_minimum_required(VERSION 3.16)
project(evac_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(evac_server evac_demo.cpp EvacPlanner.cpp) 
target_link_libraries(evac_server PRIVATE ${OpenCV_LIBS})
target_include_directories(evac_server PRIVATE ${OpenCV_INCLUDE_DIRS})
if(MSVC)
    target_compile_options(evac_server PRIVATE /utf-8)
endif()
