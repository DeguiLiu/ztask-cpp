cmake_minimum_required(VERSION 3.14)

# Fetch Catch2 v3.5.2 via ghfast.top mirror
include(FetchContent)
FetchContent_Declare(
    Catch2
    URL https://ghfast.top/https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
)
FetchContent_MakeAvailable(Catch2)

# Test executable
add_executable(ztask_tests
    test_scheduler.cpp
    test_edge_cases.cpp
)

target_link_libraries(ztask_tests PRIVATE
    ztask
    Catch2::Catch2WithMain
)

target_compile_options(ztask_tests PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(ztask_tests)
