cmake_minimum_required(VERSION 3.14)
project(NbSocket VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(nbsocket INTERFACE)

target_include_directories(nbsocket INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(BUILD_TESTING "Build the testing suite" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

option(BUILD_EXAMPLE "Build the example" ON)

if(BUILD_EXAMPLE)
    find_package(Boost QUIET COMPONENTS fiber)
    if (Boost_FOUND)
        add_subdirectory(example)
    else()
        message(STATUS "Boost.Fiber not found, skipping example")
    endif()
endif()
