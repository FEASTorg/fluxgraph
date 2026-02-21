# Tests use vcpkg-provided GoogleTest (manifest feature: tests)
find_package(GTest CONFIG REQUIRED)

# Test executable
add_executable(fluxgraph_tests
    unit/signal_store_test.cpp
    unit/namespace_test.cpp
    unit/command_test.cpp
    unit/transform_linear_test.cpp
    unit/transform_lag_test.cpp
    unit/transform_delay_test.cpp
    unit/transform_noise_test.cpp
    unit/transform_saturation_test.cpp
    unit/transform_deadband_test.cpp
    unit/transform_rate_limiter_test.cpp
    unit/transform_moving_average_test.cpp
    unit/thermal_mass_test.cpp
    unit/compiler_test.cpp
    unit/engine_test.cpp
    analytical/first_order_lag_analytical_test.cpp
    analytical/thermal_mass_analytical_test.cpp
    analytical/delay_analytical_test.cpp
    analytical/transform_analytical_test.cpp
)

target_link_libraries(fluxgraph_tests
    PRIVATE fluxgraph
    PRIVATE GTest::gtest_main
    PRIVATE GTest::gmock
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(fluxgraph_tests)

# Optional loader tests (separate executables for conditional compilation)
if(FLUXGRAPH_JSON_ENABLED)
    add_executable(json_loader_test unit/json_loader_test.cpp)
    target_link_libraries(json_loader_test
        PRIVATE fluxgraph
        PRIVATE GTest::gtest_main
    )
    gtest_discover_tests(json_loader_test)
endif()

if(FLUXGRAPH_YAML_ENABLED)
    add_executable(yaml_loader_test unit/yaml_loader_test.cpp)
    target_link_libraries(yaml_loader_test
        PRIVATE fluxgraph
        PRIVATE GTest::gtest_main
    )
    gtest_discover_tests(yaml_loader_test)
endif()

# Integration tests
add_subdirectory(integration)

# Benchmarks (optional, separate executables)
add_subdirectory(benchmarks)

# Add test command
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS fluxgraph_tests
)
