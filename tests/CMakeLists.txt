find_package(Python3 REQUIRED COMPONENTS Interpreter)
# 3.27+ can request the link graph during this configure. Older supported
# versions read query files only at startup, so their first run checks source
# boundaries; a second configure also enables configured-link verification.
set(architecture_arguments)
if(COMMAND cmake_file_api)
    cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)
    set(architecture_arguments --build-dir "${PROJECT_BINARY_DIR}")
elseif(EXISTS "${PROJECT_BINARY_DIR}/.cmake/api/v1/query/codemodel-v2")
    set(architecture_arguments --build-dir "${PROJECT_BINARY_DIR}")
endif()
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/.cmake/api/v1/query")
file(WRITE "${PROJECT_BINARY_DIR}/.cmake/api/v1/query/codemodel-v2" "")
add_test(NAME architecture.boundaries COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.py" ${architecture_arguments})
if(OWH_REVIEWED_CONSUMER)
    if(NOT EXISTS "${OWH_REVIEWED_CONSUMER}")
        message(FATAL_ERROR "OWH_REVIEWED_CONSUMER does not exist")
    endif()
endif()

function(owh_native_test target)
    add_executable(${target} ${ARGN})
    owh_configure_native(${target})
    target_link_libraries(${target} PRIVATE owh_platform owh_build_info)
    # Assertions in test sources must remain active in Release too.
    if(MSVC)
        target_compile_options(${target} PRIVATE "/FI${CMAKE_CURRENT_FUNCTION_LIST_DIR}/support/assertions.hpp")
    else()
        target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/support/assertions.hpp")
    endif()
    add_dependencies(${target} owh_native_bundle)
    set_target_properties(${target} PROPERTIES FOLDER Tests)
endfunction()

function(owh_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${OWH_NATIVE_STAGE}")
endfunction()

owh_native_test(owh_actor_signals_tests actor_signals_tests.cpp)
owh_test(platform.actor_signals_failure_cleanup owh_actor_signals_tests)
owh_native_test(owh_discovery_tests discovery_tests.cpp)
target_link_libraries(owh_discovery_tests PRIVATE owh_client)
owh_test(channel_discovery owh_discovery_tests)
owh_native_test(owh_abi_tests abi_tests.c)
target_compile_features(owh_abi_tests PRIVATE c_std_17)
target_link_libraries(owh_abi_tests PRIVATE owh_client)
owh_test(abi_layout owh_abi_tests)
owh_native_test(owh_client_tests host_bootstrap_tests.cpp)
target_link_libraries(owh_client_tests PRIVATE owh_client_fixture)
owh_test(host_bootstrap_identity owh_client_tests $<TARGET_FILE:owh_agent>)

owh_native_test(owh_pixel_stream_tests pixel_stream_tests.cpp)
target_link_libraries(owh_pixel_stream_tests PRIVATE owh_ipc)
owh_test(pixel_stream.child_process owh_pixel_stream_tests)
owh_native_test(owh_service_channel_tests service_channel_tests.cpp)
target_link_libraries(owh_service_channel_tests PRIVATE owh_agent_fixture)
owh_test(service_channel.session owh_service_channel_tests)
owh_native_test(owh_cpp_api_tests cpp_api_tests.cpp)
target_link_libraries(owh_cpp_api_tests PRIVATE OverwolfHook::Cpp)
owh_test(cpp_api.contract owh_cpp_api_tests)
owh_native_test(owh_agent_server_tests agent_server_tests.cpp)
target_link_libraries(owh_agent_server_tests PRIVATE owh_agent_fixture owh_client)
set_target_properties(owh_agent_server_tests PROPERTIES OUTPUT_NAME Overwolf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/agent-server/$<CONFIG>")
foreach(module IN ITEMS OverWolf.Client.Core.dll OWServer.dll)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/agent-server/$<CONFIG>/${module}"
        CONTENT "Owned main-package role fixture; never loaded.\n")
endforeach()
owh_test(agent_server.multiclient_probe owh_agent_server_tests)
set_tests_properties(agent_server.multiclient_probe PROPERTIES RUN_SERIAL TRUE)
owh_native_test(owh_overlay_public_flow_tests overlay_public_flow_tests.cpp)
target_link_libraries(owh_overlay_public_flow_tests PRIVATE owh_client_fixture owh_agent_fixture)
owh_test(overlay_public_flow.pixel_buffer_fixture owh_overlay_public_flow_tests)
  owh_test(overlay_public_flow.managed_submission owh_overlay_public_flow_tests managed)
owh_test(overlay_public_flow.malformed_info owh_overlay_public_flow_tests malformed)
owh_test(overlay_public_flow.readiness_recovers owh_overlay_public_flow_tests readiness)
owh_test(overlay_public_flow.allocation_rollback owh_overlay_public_flow_tests allocation)
foreach(case RANGE 1 8)
    owh_test(overlay_public_flow.corrupt_capability_${case} owh_overlay_public_flow_tests caps-corrupt-${case})
endforeach()
owh_test(capture_public_flow.events owh_overlay_public_flow_tests capture)
owh_test(capture_public_flow.malformed_info owh_overlay_public_flow_tests capture-malformed)
owh_test(overlay_public_flow.child_reply_loss owh_overlay_public_flow_tests drop-reply)
owh_native_test(owh_overlay_pipeline_tests overlay_pipeline_tests.cpp)
target_link_libraries(owh_overlay_pipeline_tests PRIVATE owh_client_fixture owh_agent_fixture)
owh_test(overlay_pipeline.owned_publisher owh_overlay_pipeline_tests)
owh_native_test(owh_d3d11_raw_resource_tests d3d11_raw_resource_tests.cpp)
target_link_libraries(owh_d3d11_raw_resource_tests PRIVATE owh_agent_fixture)
owh_test(d3d11_raw_resource.owned_child owh_d3d11_raw_resource_tests)
set_tests_properties(d3d11_raw_resource.owned_child PROPERTIES SKIP_RETURN_CODE 77)
owh_native_test(owh_profile_resolver_tests profile_resolver_tests.cpp)
target_link_libraries(owh_profile_resolver_tests PRIVATE owh_overwolf_passive bcrypt)
owh_test(runtime_profile_resolver.synthetic_pe owh_profile_resolver_tests)
add_executable(owh_overwolf_process_fixture fixtures/overwolf_process.cpp)
owh_configure_native(owh_overwolf_process_fixture)
set_target_properties(owh_overwolf_process_fixture PROPERTIES OUTPUT_NAME Overwolf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/overwolf-process/$<CONFIG>" FOLDER Tests/Fixtures)
foreach(module IN ITEMS OverWolf.Client.Core.dll OWServer.dll)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/overwolf-process/$<CONFIG>/${module}"
        CONTENT "Owned main-package role fixture; never loaded.\n")
endforeach()
add_custom_command(TARGET owh_overwolf_process_fixture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/overwolf-decoy/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:owh_overwolf_process_fixture>"
        "${CMAKE_CURRENT_BINARY_DIR}/overwolf-decoy/$<CONFIG>/Overwolf.exe")
owh_native_test(owh_overwolf_control_tests overwolf_control_tests.cpp)
target_link_libraries(owh_overwolf_control_tests PRIVATE owh_client_fixture)
owh_test(overwolf.control owh_overwolf_control_tests $<TARGET_FILE:owh_overwolf_process_fixture>
    "${CMAKE_CURRENT_BINARY_DIR}/overwolf-decoy/$<CONFIG>/Overwolf.exe")
set_tests_properties(overwolf.control PROPERTIES RUN_SERIAL TRUE)

owh_native_test(owh_lifetime_admission_tests lifetime_admission_tests.cpp)
target_link_libraries(owh_lifetime_admission_tests PRIVATE owh_agent_fixture)
owh_test(runtime_lifetime_admission.owned_fixture owh_lifetime_admission_tests)
owh_native_test(owh_controller_tests controller_tests.cpp)
owh_test(controller_stopping owh_controller_tests)

owh_native_test(owh_state_tests state_tests.cpp)
target_link_libraries(owh_state_tests PRIVATE owh_client)
foreach(case IN ITEMS roundtrip bounds availability lifecycle contention cpp-ownership)
    owh_test(state.${case} owh_state_tests ${case})
endforeach()
owh_native_test(owh_event_log_cursor_tests event_log_cursor_tests.cpp)
target_link_libraries(owh_event_log_cursor_tests PRIVATE owh_ipc)
foreach(case IN ITEMS roundtrip notifications overflow process crash corrupt abandoned)
    owh_test(event_log_cursor.${case} owh_event_log_cursor_tests ${case})
endforeach()
owh_native_test(owh_reader_tests reader_tests.cpp)
target_link_libraries(owh_reader_tests PRIVATE owh_overwolf_passive)
owh_test(reader_signature owh_reader_tests)
owh_native_test(owh_provider_sources_tests provider_sources_tests.cpp)
target_link_libraries(owh_provider_sources_tests PRIVATE owh_overwolf_passive)
owh_test(provider_sources.signature_and_values owh_provider_sources_tests)
owh_native_test(owh_channel_tests channel_tests.cpp)
target_link_libraries(owh_channel_tests PRIVATE owh_overwolf_passive)
owh_test(event_channels owh_channel_tests)

owh_test(capture_inspection ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/inspect_capture_test.py")
owh_test(ffm_generation.atomic_output ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/ffm_generation_tests.py")
if(OWH_BUILD_TOOLS)
    owh_test(controller_cli_identity ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/controller_cli_test.py" $<TARGET_FILE:owh_probe>)
    owh_test(info_recording ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/info_recorder_test.py" $<TARGET_FILE:owh_info>)
endif()
if(OWH_HOOK_TESTS)
    add_library(owh_hook_fixture SHARED hook_fixture.cpp)
    if(MSVC)
        target_compile_options(owh_hook_fixture PRIVATE /Od /Ob0 /GL-)
    endif()
    set_target_properties(owh_hook_fixture PROPERTIES FOLDER Tests/Fixtures)
    owh_native_test(owh_hook_tests hook_tests.cpp)
    target_link_libraries(owh_hook_tests PRIVATE minhook owh_client_fixture)
    owh_test(hook_trampoline owh_hook_tests $<TARGET_FILE:owh_hook_fixture>)
endif()
if(OWH_REVIEWED_CONSUMER)
    owh_native_test(owh_subscription_backend_tests event_hub_tests.cpp)
    target_include_directories(owh_subscription_backend_tests PRIVATE "${PROJECT_SOURCE_DIR}/modules/agent/src")
    target_link_libraries(owh_subscription_backend_tests PRIVATE owh_agent_core owh_client_fixture)
    owh_test(event_hub.owned_consumer owh_subscription_backend_tests "${OWH_REVIEWED_CONSUMER}")
    owh_test(event_source.prepared_export owh_subscription_backend_tests "${OWH_REVIEWED_CONSUMER}" prepared)
    set_tests_properties(event_hub.owned_consumer event_source.prepared_export PROPERTIES RUN_SERIAL TRUE)
endif()

# Compile both the generated proxy and router against one isolated catalog.
set(catalog_include "${CMAKE_CURRENT_BINARY_DIR}/catalog-probe/include")
add_custom_command(OUTPUT "${catalog_include}/owh/core/events.def"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/catalog_generation_tests.py"
        --output "${catalog_include}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog_generation_tests.py"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/service_contracts.hpp"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/types.hpp"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/services.hpp"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/events.def"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/overlays.def"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/session.def"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/policy.hpp"
        "${PROJECT_SOURCE_DIR}/modules/core/include/owh/core/result.hpp"
    VERBATIM)
add_custom_target(owh_catalog_fixture DEPENDS "${catalog_include}/owh/core/events.def")
owh_native_test(owh_catalog_generation_tests catalog_generation_probe.cpp
    "${PROJECT_SOURCE_DIR}/modules/ipc/src/services.cpp")
add_dependencies(owh_catalog_generation_tests owh_catalog_fixture)
target_include_directories(owh_catalog_generation_tests BEFORE PRIVATE "${catalog_include}")
target_link_libraries(owh_catalog_generation_tests PRIVATE owh_ipc)
owh_test(services.catalog_extension owh_catalog_generation_tests)

# Owned DLLs model the ordinary load -> lookup -> first-run startup ordering.
add_library(owh_startup_agent_fixture SHARED fixtures/startup_agent.cpp)
add_library(owh_startup_server_fixture SHARED fixtures/startup_server.cpp)
foreach(target IN ITEMS owh_startup_agent_fixture owh_startup_server_fixture)
    owh_configure_native(${target})
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/startup" FOLDER Tests/Fixtures)
    if(MSVC)
        # The throwing startup case models a C++ initializer escaping a named
        # DLL export; do not assume extern-C functions are nonthrowing here.
        target_compile_options(${target} PRIVATE /Od /Ob0 /GL- /EHc-)
    endif()
endforeach()
set_target_properties(owh_startup_agent_fixture PROPERTIES OUTPUT_NAME OWAgent)
set_target_properties(owh_startup_server_fixture PROPERTIES OUTPUT_NAME OWServer)
owh_native_test(owh_startup_bridge_tests startup_bridge_tests.cpp)
target_link_libraries(owh_startup_bridge_tests PRIVATE owh_overwolf_fixture)
add_dependencies(owh_startup_bridge_tests owh_startup_agent_fixture owh_startup_server_fixture)
foreach(case IN ITEMS ordering late null throw failed missing)
    owh_test(startup_bridge.${case} owh_startup_bridge_tests ${case}
        $<TARGET_FILE:owh_startup_server_fixture> $<TARGET_FILE:owh_startup_agent_fixture>)
endforeach()
