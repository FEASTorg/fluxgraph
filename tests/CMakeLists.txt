# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

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
)

target_link_libraries(fluxgraph_tests
    PRIVATE fluxgraph
    PRIVATE gtest_main
    PRIVATE gmock
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(fluxgraph_tests)

# Add test command
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS fluxgraph_tests
)
