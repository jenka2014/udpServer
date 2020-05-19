cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

# set the project name
project(udpServer VERSION 2.0 LANGUAGES CXX)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(udpServer 
    src/udpServer.cpp
    src/md5.cpp 
    include/date.h
    include/md5.h)

set_target_properties(udpServer PROPERTIES LINKER_LANGUAGE CXX)


