cmake_minimum_required(VERSION 3.16)
project(fin-tracker-bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fin-tracker-bot src/main.cpp)
target_compile_options(fin-tracker-bot PRIVATE -Wall -Wextra)

find_package(dpp QUIET)
if(dpp_FOUND)
    target_link_libraries(fin-tracker-bot PRIVATE dpp::dpp)
else()
    # Fallback for installs that don't ship a CMake config
    target_link_libraries(fin-tracker-bot PRIVATE dpp)
endif()
