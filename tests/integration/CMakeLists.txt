# Integration test executable
add_executable(fluxgraph_integration_tests
    determinism_test.cpp
    multi_model_test.cpp
)

target_link_libraries(fluxgraph_integration_tests
    PRIVATE
    fluxgraph
    GTest::gtest_main
)

# Register with CTest
include(GoogleTest)
gtest_discover_tests(fluxgraph_integration_tests)
