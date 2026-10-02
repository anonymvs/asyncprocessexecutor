cmake_minimum_required(VERSION 3.5.0)
project(asyncprocessexecutor VERSION 0.1.0 LANGUAGES C CXX)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(GLOB HEADERS "include/*.h")
file(GLOB SOURCES "src/*.cpp")

#add_library(asyncprocessexecutor ${SOURCES} ${HEADERS})
#add_library(asyncprocessexecutor src/asyncprocessexecutor.cpp include/asyncprocessexecutor.h)
add_library(asyncprocessexecutor src/async_task_flow.cpp include/async_task_flow.h)
target_include_directories(asyncprocessexecutor PRIVATE include src)

enable_testing()

include(GoogleTest)

file(GLOB TESTS "test/*.cpp")
add_executable(asyncprocessexecutor_test ${TESTS})

target_include_directories(asyncprocessexecutor_test PRIVATE include)

target_link_libraries(
    asyncprocessexecutor_test
    asyncprocessexecutor
    GTest::gtest_main
)

gtest_discover_tests(asyncprocessexecutor_test)

