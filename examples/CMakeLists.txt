cmake_minimum_required(VERSION 3.14)

# Helper function to add examples
function(add_ztask_example target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ztask)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

# Basic demo
add_ztask_example(basic_demo basic_demo.cpp)

# Low power demo
add_ztask_example(low_power_demo low_power_demo.cpp)

# Dynamic demo
add_ztask_example(dynamic_demo dynamic_demo.cpp)

# Benchmark (C vs C++14 performance comparison)
add_ztask_example(ztask_benchmark benchmark.cpp)
target_compile_options(ztask_benchmark PRIVATE -O3)
