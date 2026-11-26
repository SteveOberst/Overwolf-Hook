set(client_sources
    src/abi/sdk.cpp
    src/abi/overlays.cpp src/abi/wait.cpp
    src/abi/events.cpp
    src/abi/game_state.cpp
    src/abi/overwolf.cpp
    src/abi/handles.hpp
    src/sdk.cpp
    src/sdk.hpp
    src/events/subscription.cpp
    src/events/subscription.hpp
    src/session/target.cpp
    src/overwolf/process.hpp
    src/overwolf/installation.cpp
    src/overwolf/control.cpp
    src/session/preflight.cpp
    src/session/bootstrap.cpp
    src/session/attached_session.cpp
    src/session/attached_session.hpp
    src/session/target.hpp
    src/session/bootstrap_payload.hpp
    src/overlays/overlay.cpp
    src/overlays/overlay.hpp)
# The DLL and independently compiled test variant share these requirements.
function(owh_configure_client_implementation target)
    owh_configure_native(${target})
    target_compile_definitions(${target} PRIVATE OWH_BUILD_owh_client)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src")
    target_link_libraries(${target} PRIVATE
        owh_abi owh_platform owh_ipc owh_overwolf_passive owh_build_info bcrypt advapi32 version shell32)
endfunction()

add_library(owh_client SHARED ${client_sources} "${OWH_VERSION_RESOURCE}")
add_library(OverwolfHook::Client ALIAS owh_client)
owh_configure_client_implementation(owh_client)
target_sources(owh_client PUBLIC FILE_SET api_headers TYPE HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include" FILES ${OWH_PUBLIC_HEADERS})
target_include_directories(owh_client INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
set_target_properties(owh_client PROPERTIES
    PREFIX "" OUTPUT_NAME owh_client EXPORT_NAME Client FOLDER Native/Client)
install(TARGETS owh_client EXPORT OverwolfHookTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET api_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING)
    include(testing.cmake)
endif()
