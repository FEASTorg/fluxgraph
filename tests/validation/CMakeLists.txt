set(FLUXGRAPH_VALIDATION_TARGETS)

add_executable(validation_thermal_mass thermal_mass_validation.cpp)
target_link_libraries(validation_thermal_mass PRIVATE fluxgraph)
list(APPEND FLUXGRAPH_VALIDATION_TARGETS validation_thermal_mass)

add_custom_target(fluxgraph_validations
    DEPENDS ${FLUXGRAPH_VALIDATION_TARGETS}
)
