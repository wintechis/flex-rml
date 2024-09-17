# Set the minimum CMake version required
cmake_minimum_required(VERSION 3.15)

# Project name and language standard
project(flexrml VERSION 1.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find the serd package, which you installed via vcpkg
find_package(serd CONFIG REQUIRED)

# Compiler and flags (similar to CFLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -pedantic -Wall -Wextra -O3")

# Include directories (similar to INCLUDES)
include_directories(
    .
    ./parser
)

# Gather all source files (similar to SOURCES)
file(GLOB SOURCES
    "*.cpp"
    "rdf_parser/*.cpp"
    "file_reader/*.cpp"
)

# Add the executable target
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directories for the target
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/parser)

# Link the serd library to the executable target
target_link_libraries(${PROJECT_NAME} PRIVATE serd::serd)
