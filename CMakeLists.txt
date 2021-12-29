# Set the minimum CMake version
cmake_minimum_required(VERSION 3.0.0)

# Set the project name
project(LockFreeQueue VERSION 0.1.0)

# Define the C++ standard
set(CMAKE_CXX_STANDARD 20)

# Enable testing
include(CTest)
enable_testing()

#Bring the headers into the project
include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

# Define the project sources
file(GLOB LIBRARY_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)

# Build the DLL
add_library(LockFreeQueue SHARED ${LIBRARY_SOURCES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

# Define the test sources.
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/testing/src/*.cpp)

# Create the test for the library.
add_executable(LockFreeQueueTest ${LIBRARY_SOURCES} ${TEST_SOURCES})
target_link_libraries(LockFreeQueueTest LockFreeQueue)

# define tests
add_test(
  NAME LockFreeQueueTest
  COMMAND $<TARGET_FILE:LockFreeQueueTest>)