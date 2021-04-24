cmake_minimum_required(VERSION 3.16)
project(ibvBench)

set(CMAKE_CXX_STANDARD 14)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")
include(ibvBenchTools)
find_package(Fabric REQUIRED)

option(BUILD_RDMA-CORE "build rdma-core examples" OFF)
if (BUILD_RDMA-CORE)
    add_subdirectory(rdma-core)
endif()
add_subdirectory(modules)
add_subdirectory(benchmarks)