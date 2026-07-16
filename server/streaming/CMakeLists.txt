cmake_minimum_required(VERSION 3.16)
project(streaming)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

add_executable(streaming stream_receiver.cpp)
target_include_directories(streaming PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(streaming PRIVATE ${OpenCV_LIBS} Threads::Threads)