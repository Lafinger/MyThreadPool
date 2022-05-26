cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(ThreadPool VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

#add_library(ThreadPoolLib STATIC ThreadPool.hpp)

add_executable(ThreadPool main.cpp ThreadPool.hpp)
target_link_libraries(ThreadPool -pthread)
#target_link_libraries(ThreadPoolExe ThreadPoolLib)