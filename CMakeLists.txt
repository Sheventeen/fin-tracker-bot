cmake_minimum_required(VERSION 3.16)
project(fin-tracker-bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # build/compile_commands.json, used by editors

add_compile_options(-Wall -Wextra)

find_package(SQLite3 REQUIRED)  # sudo apt install libsqlite3-dev
find_package(Threads REQUIRED)

# Everything that doesn't need Discord: parsing, money, periods, database.
# The bot and the tests both link it, so tests build without DPP.
add_library(fin-tracker-core STATIC
    src/db.cpp
    src/money.cpp
    src/parser.cpp
    src/periods.cpp
    src/text.cpp
)
target_include_directories(fin-tracker-core PUBLIC src)
target_link_libraries(fin-tracker-core PUBLIC SQLite::SQLite3 Threads::Threads)

add_executable(fin-tracker-bot src/main.cpp src/commands.cpp)
target_link_libraries(fin-tracker-bot PRIVATE fin-tracker-core)

find_package(dpp QUIET)
if(dpp_FOUND)
    target_link_libraries(fin-tracker-bot PRIVATE dpp::dpp)
else()
    # Fallback for installs that don't ship a CMake config
    target_link_libraries(fin-tracker-bot PRIVATE dpp)
endif()

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE fin-tracker-core)
add_test(NAME core_tests COMMAND core_tests)
