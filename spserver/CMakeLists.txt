
# Minimum CMake version required
cmake_minimum_required(VERSION 3.0)

# Project name
project(CustomProject)

# Compiler settings
set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -D_REENTRANT -D_GNU_SOURCE -g -fPIC")

# Linker settings
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lstdc++ -lpthread")

# Include directories
include_directories(${HOME}/libevent/)

# Library directories
link_directories(${HOME}/libevent/.libs)

# Source files
set(SOURCE_FILES
    # Add your source files here
)

# Executable target
add_executable(CustomExecutable ${SOURCE_FILES})

# Link libraries
target_link_libraries(CustomExecutable event rt)
