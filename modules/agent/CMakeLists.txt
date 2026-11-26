set(agent_core_sources
    src/agent_session.cpp
    src/agent_session.hpp
    src/session_context.hpp
    src/overlays/overlay_service.hpp
    src/overlays/overlay_service.cpp
    src/events/event_service.hpp
    src/events/event_service.cpp
    src/agent_server.cpp
    src/agent_server.hpp
    src/agent_root.cpp
    src/agent_root.hpp
    src/overlays/overlay_domain.cpp
    src/overlays/overlay_domain.hpp
    src/events/event_hub.cpp
    src/events/event_hub.hpp)
function(owh_configure_agent_implementation target backend)
    owh_configure_native(${target})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src")
    target_link_libraries(${target}
        PUBLIC owh_platform owh_ipc owh_overwolf_passive ${backend}
        PRIVATE owh_build_info advapi32)
endfunction()

add_library(owh_agent_core STATIC ${agent_core_sources})
owh_configure_agent_implementation(owh_agent_core owh_overwolf_active)
set_target_properties(owh_agent_core PROPERTIES FOLDER Native/Agent)

add_library(owh_agent SHARED src/entry.cpp "${OWH_VERSION_RESOURCE}")
add_library(OverwolfHook::Agent ALIAS owh_agent)
owh_configure_native(owh_agent)
target_include_directories(owh_agent PRIVATE src)
target_link_libraries(owh_agent PRIVATE owh_agent_core owh_build_info advapi32)
set_target_properties(owh_agent PROPERTIES
    PREFIX "" OUTPUT_NAME owh_agent EXPORT_NAME Agent FOLDER Native/Agent)
install(TARGETS owh_agent EXPORT OverwolfHookTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(BUILD_TESTING)
    include(testing.cmake)
endif()
