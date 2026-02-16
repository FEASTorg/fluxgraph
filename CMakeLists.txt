cmake_minimum_required(VERSION 3.20)
project(fluxgraph VERSION 0.1.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(FLUXGRAPH_BUILD_TESTS "Build unit tests" ON)
option(FLUXGRAPH_BUILD_EXAMPLES "Build examples" ON)

# Core library sources
set(FLUXGRAPH_SOURCES
    src/core/signal_store.cpp
    src/core/namespace.cpp
)

# Create static library
add_library(fluxgraph STATIC ${FLUXGRAPH_SOURCES})

# Public include directory
target_include_directories(fluxgraph PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compiler warnings
if(MSVC)
    target_compile_options(fluxgraph PRIVATE /W4 /WX)
else()
    target_compile_options(fluxgraph PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Tests
if(FLUXGRAPH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(FLUXGRAPH_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
install(TARGETS fluxgraph
    EXPORT fluxgraphTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/fluxgraph
    DESTINATION include
)

install(EXPORT fluxgraphTargets
    FILE fluxgraphTargets.cmake
    NAMESPACE fluxgraph::
    DESTINATION lib/cmake/fluxgraph
)
