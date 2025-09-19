cmake_minimum_required(VERSION 3.16)
project(StandardCPPServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chat_server
    src/main.cpp
    src/chat_server.cpp
)

target_include_directories(chat_server PRIVATE include)

target_compile_options(chat_server PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(chat_server PRIVATE pthread)
