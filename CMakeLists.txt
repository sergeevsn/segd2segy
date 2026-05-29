cmake_minimum_required(VERSION 3.16)
project(segd2segy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SEGD2SEGY_BUILD_TESTS "Build segd2segy tests" OFF)

add_subdirectory(segdcore)
add_subdirectory(src)

if(SEGD2SEGY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
