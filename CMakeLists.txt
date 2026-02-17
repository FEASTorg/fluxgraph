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
option(FLUXGRAPH_BUILD_SERVER "Build gRPC server" OFF)

# Optional loader support
option(FLUXGRAPH_JSON_ENABLED "Enable JSON graph loading (requires nlohmann/json)" OFF)
option(FLUXGRAPH_YAML_ENABLED "Enable YAML graph loading (requires yaml-cpp)" OFF)

# FetchContent for optional dependencies
include(FetchContent)

# Core library sources
set(FLUXGRAPH_SOURCES
    src/core/signal_store.cpp
    src/core/namespace.cpp
    src/model/thermal_mass.cpp
    src/graph/compiler.cpp
    src/engine.cpp
)

# Conditionally add JSON loader source
if(FLUXGRAPH_JSON_ENABLED)
    list(APPEND FLUXGRAPH_SOURCES src/loaders/json_loader.cpp)
    
    # Fetch nlohmann/json
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Conditionally add YAML loader source
if(FLUXGRAPH_YAML_ENABLED)
    list(APPEND FLUXGRAPH_SOURCES src/loaders/yaml_loader.cpp)
    
    # Fetch yaml-cpp (use master branch for CMake 3.20+ compatibility)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG master
    )
    # Set yaml-cpp options before making it available
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

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

# Link optional dependencies
if(FLUXGRAPH_JSON_ENABLED)
    target_link_libraries(fluxgraph PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(fluxgraph PUBLIC FLUXGRAPH_JSON_ENABLED)
endif()

if(FLUXGRAPH_YAML_ENABLED)
    target_link_libraries(fluxgraph PRIVATE yaml-cpp::yaml-cpp)
    target_compile_definitions(fluxgraph PUBLIC FLUXGRAPH_YAML_ENABLED)
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

# gRPC Server
if(FLUXGRAPH_BUILD_SERVER)
    add_subdirectory(server)
endif()

# TODO(Phase 23, Week 5): Fix install rules for optional dependencies
# Installation currently disabled due to CMake limitation with PRIVATE dependencies in install(EXPORT)
# Will be fixed in Week 5 Task 5.1 with proper cmake/fluxgraphConfig.cmake.in
#
# install(TARGETS fluxgraph
#     EXPORT fluxgraphTargets
#     ARCHIVE DESTINATION lib
#     LIBRARY DESTINATION lib
#     RUNTIME DESTINATION bin
# )
#
# install(DIRECTORY include/fluxgraph
#     DESTINATION include
# )
#
# install(EXPORT fluxgraphTargets
#     FILE fluxgraphTargets.cmake
#     NAMESPACE fluxgraph::
#     DESTINATION lib/cmake/fluxgraph
# )

