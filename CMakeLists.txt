cmake_minimum_required(VERSION 3.16)
project(ibvBench)

set(CMAKE_CXX_STANDARD 14)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")
include(ibvBenchTools)
find_package(Fabric REQUIRED)

add_subdirectory(modules)
add_subdirectory(rdma-core)
add_subdirectory(benchmarks)