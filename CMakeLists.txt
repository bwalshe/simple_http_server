cmake_minimum_required(VERSION 3.10)

project(SimpleServer)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES src/util.cpp 
    src/connection.cpp
    src/request.cpp 
    src/request_processor.cpp
    src/simple_server.cpp)

set(TESTS test/test_main.cpp 
    test/test_queue.cpp
    test/test_threadpool.cpp)

include_directories(src)
add_executable(http_server ${SOURCES})
target_compile_options(http_server PRIVATE -Wall -Wextra -pedantic -Werror)
find_package(TBB)
target_link_libraries(http_server PRIVATE TBB::tbb pthread)

find_package(Catch2 REQUIRED)
add_executable(test ${TESTS})
target_link_libraries(test PRIVATE Catch2::Catch2 pthread)
