if(FLUXGRAPH_YAML_ENABLED)
    add_executable(example_yaml_graph main.cpp)
    target_link_libraries(example_yaml_graph PRIVATE fluxgraph)
    
    # Copy graph.yaml to build directory
    add_custom_command(TARGET example_yaml_graph POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/graph.yaml
        $<TARGET_FILE_DIR:example_yaml_graph>/graph.yaml
    )
endif()
