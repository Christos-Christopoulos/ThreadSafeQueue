# Set the minimum CMake version
cmake_minimum_required(VERSION 3.0.0)

# Set the project name
project(LockFreeQueue VERSION 0.1.0)

# Define the C++ standard
set(CMAKE_CXX_STANDARD 20)

# Enable testing.
include(CTest)
enable_testing()

# Build the tests.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)

#Bring the headers into the project
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Define the project sources
file(GLOB LIBRARY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Build the DLL
add_library(LockFreeQueue SHARED ${LIBRARY_SOURCES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
