cmake_minimum_required(VERSION 3.21)
project(StockfishTheSpire CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(STS_BUILD_TESTS "Build unit tests" ON)
option(STS_BUILD_BENCHMARKS "Build benchmarks" ON)
option(STS_ENABLE_SANITIZERS "Build with ASan/UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

add_subdirectory(src/engine)

if(STS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(STS_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
