cmake_minimum_required(VERSION 3.16)
project(image_to_bitmap)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(image_to_bitmap image_to_bitmap.cpp)
target_link_libraries(image_to_bitmap PRIVATE ${OpenCV_LIBS})
target_include_directories(image_to_bitmap PRIVATE ${OpenCV_INCLUDE_DIRS})
if(MSVC)
    target_compile_options(image_to_bitmap PRIVATE /utf-8)
endif()

add_executable(probe_color probe_color.cpp)
target_link_libraries(probe_color PRIVATE ${OpenCV_LIBS})
target_include_directories(probe_color PRIVATE ${OpenCV_INCLUDE_DIRS})
if(MSVC)
    target_compile_options(probe_color PRIVATE /utf-8)
endif()
