cmake_minimum_required(VERSION 3.15)

# Project name
project(flexrml VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find packages from vcpkg
find_package(serd CONFIG REQUIRED)
find_package(cityhash CONFIG REQUIRED)

# Compiler and flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -pedantic -Wall -Wextra -O3")

# Include directories
include_directories(
    .
    ./parser
)

# Gather all source files
file(GLOB SOURCES
    "*.cpp"
    "file_reader/*.cpp"
)

# executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/parser)

# Link the libraries to the executable target
target_link_libraries(${PROJECT_NAME} PRIVATE serd::serd)
target_link_libraries(${PROJECT_NAME} PRIVATE cityhash)
