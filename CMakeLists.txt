cmake_minimum_required(VERSION 3.10)

project(SimpleServer)

set(SOURCES src/util.cpp 
    src/connection.cpp
    src/request.cpp 
    src/connection_queue.cpp 
    src/simple_server.cpp)

include_directories(src)
add_executable(http_server ${SOURCES})
