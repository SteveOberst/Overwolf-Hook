add_executable(owh_info info.cpp)
add_executable(owh_probe probe.cpp)
foreach(target IN ITEMS owh_info owh_probe)
    owh_configure_native(${target})
    target_link_libraries(${target} PRIVATE owh_cpp)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OWH_NATIVE_DIRECTORY}" FOLDER Tools)
endforeach()
set_target_properties(owh_info PROPERTIES OUTPUT_NAME owh_info)
set_target_properties(owh_probe PROPERTIES OUTPUT_NAME owh_controller)

# The active capture tool deploys the Agent payload; passive info reads do not.
add_dependencies(owh_probe owh_agent)
