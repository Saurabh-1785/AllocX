cmake_minimum_required(VERSION 3.14)
project(AllocX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Release optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Library sources
set(ALLOCX_SOURCES
    src/stack_allocator.cpp
    src/pool_allocator.cpp
    src/freelist_allocator.cpp
)

# Static library
add_library(allocx STATIC ${ALLOCX_SOURCES})
target_include_directories(allocx PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Benchmarks
add_executable(allocx_benchmark benchmarks/benchmark_main.cpp)
target_link_libraries(allocx_benchmark allocx)

# Tests
add_executable(allocx_tests tests/test_main.cpp)
target_link_libraries(allocx_tests allocx)

# Examples
add_executable(basic_usage examples/basic_usage.cpp)
target_link_libraries(basic_usage allocx)

add_executable(stl_integration examples/stl_integration.cpp)
target_link_libraries(stl_integration allocx)

# Installation (optional)
install(TARGETS allocx ARCHIVE DESTINATION lib)
install(DIRECTORY include/allocx DESTINATION include)
