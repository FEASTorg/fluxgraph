# Benchmark executables
add_executable(benchmark_signal_store signal_store_bench.cpp)
target_link_libraries(benchmark_signal_store PRIVATE fluxgraph)

add_executable(benchmark_namespace namespace_bench.cpp)
target_link_libraries(benchmark_namespace PRIVATE fluxgraph)

add_executable(benchmark_tick tick_bench.cpp)
target_link_libraries(benchmark_tick PRIVATE fluxgraph)

# Optional loader benchmarks
if(FLUXGRAPH_JSON_ENABLED)
    add_executable(json_loader_bench json_loader_bench.cpp)
    target_link_libraries(json_loader_bench PRIVATE fluxgraph)
endif()

if(FLUXGRAPH_YAML_ENABLED)
    add_executable(yaml_loader_bench yaml_loader_bench.cpp)
    target_link_libraries(yaml_loader_bench PRIVATE fluxgraph)
endif()
