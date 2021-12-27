cmake_minimum_required(VERSION 3.0.0)
project(LockFreeQueue VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 20)

include(CTest)
enable_testing()

set(LockFreeQueue_SOURCES 
    ${CMAKE_CURRENT_LIST_DIR}/LockFreeQueue.h
    ${CMAKE_CURRENT_LIST_DIR}/LockFreeQueue.cpp )

add_library(LockFreeQueue SHARED ${LockFreeQueue_SOURCES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

set(LockFreeQueueTest_SOURCES 
    ${CMAKE_CURRENT_LIST_DIR}/LockFreeQueue.h
    ${CMAKE_CURRENT_LIST_DIR}/LockFreeQueueTest.cpp )

add_executable(LockFreeQueueTest ${LockFreeQueueTest_SOURCES})
target_link_libraries(LockFreeQueueTest LockFreeQueue)

# define tests
add_test(
  NAME LockFreeQueueTest
  COMMAND $<TARGET_FILE:LockFreeQueueTest>)