cmake_minimum_required(VERSION 3.10)
project(csvdb CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-O3 -Wall -Wextra -Wpedantic)

add_definitions(-DKXVER=3)

include_directories(src/include)

add_library(csvdb SHARED src/lib/csvdb.cpp)
set_target_properties(csvdb PROPERTIES
    PREFIX ""
    OUTPUT_NAME "csvdb"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/install"
)
