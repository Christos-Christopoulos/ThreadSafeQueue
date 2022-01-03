#Bring the headers into the project
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Define the test sources.
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Create the test for the library.
add_executable(LockFreeQueueTest ${TEST_SOURCES})
target_include_directories(LockFreeQueueTest PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(LockFreeQueueTest LockFreeQueue)

# define tests
add_test(NAME LockFreeQueueTest COMMAND $<TARGET_FILE:LockFreeQueueTest>)
