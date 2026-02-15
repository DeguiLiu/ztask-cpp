cmake_minimum_required(VERSION 3.14)
project(ztask VERSION 1.0.0 LANGUAGES CXX)

# Header-only INTERFACE library
add_library(ztask INTERFACE)
target_include_directories(ztask INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ztask INTERFACE cxx_std_14)

# Build options
option(ZTASK_BUILD_TESTS "Build tests" ON)
option(ZTASK_BUILD_EXAMPLES "Build examples" ON)

# Tests
if(ZTASK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(ZTASK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install
include(CMakePackageConfigHelpers)
install(TARGETS ztask EXPORT ztaskTargets)
install(DIRECTORY include/ztask DESTINATION include)
install(EXPORT ztaskTargets
    FILE ztaskConfig.cmake
    NAMESPACE ztask::
    DESTINATION lib/cmake/ztask)
