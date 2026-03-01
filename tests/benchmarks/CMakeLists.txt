set(FLUXGRAPH_BENCHMARK_TARGETS)

# Benchmark executables
add_executable(benchmark_signal_store signal_store_bench.cpp)
target_link_libraries(benchmark_signal_store PRIVATE fluxgraph)
list(APPEND FLUXGRAPH_BENCHMARK_TARGETS benchmark_signal_store)

add_executable(benchmark_namespace namespace_bench.cpp)
target_link_libraries(benchmark_namespace PRIVATE fluxgraph)
list(APPEND FLUXGRAPH_BENCHMARK_TARGETS benchmark_namespace)

add_executable(benchmark_tick tick_bench.cpp)
target_link_libraries(benchmark_tick PRIVATE fluxgraph)
list(APPEND FLUXGRAPH_BENCHMARK_TARGETS benchmark_tick)

# Optional loader benchmarks
if(FLUXGRAPH_JSON_ENABLED)
    add_executable(json_loader_bench json_loader_bench.cpp)
    target_link_libraries(json_loader_bench PRIVATE fluxgraph)
    list(APPEND FLUXGRAPH_BENCHMARK_TARGETS json_loader_bench)
endif()

if(FLUXGRAPH_YAML_ENABLED)
    add_executable(yaml_loader_bench yaml_loader_bench.cpp)
    target_link_libraries(yaml_loader_bench PRIVATE fluxgraph)
    list(APPEND FLUXGRAPH_BENCHMARK_TARGETS yaml_loader_bench)
endif()

add_custom_target(fluxgraph_benchmarks
    DEPENDS ${FLUXGRAPH_BENCHMARK_TARGETS}
)
