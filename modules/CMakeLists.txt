# One public C contract is shared by native modules, installation and FFM.
set(public_header_manifest "${PROJECT_SOURCE_DIR}/cmake/public-headers.txt")
file(STRINGS "${public_header_manifest}" public_header_names)
set(OWH_PUBLIC_HEADERS)
foreach(name IN LISTS public_header_names)
    list(APPEND OWH_PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/include/overwolf_hook/${name}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${public_header_manifest}")
add_library(owh_abi INTERFACE)
set_target_properties(owh_abi PROPERTIES EXPORT_NAME Abi)
add_library(OverwolfHook::Abi ALIAS owh_abi)
target_include_directories(owh_abi INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_sources(owh_abi PRIVATE ${OWH_PUBLIC_HEADERS})
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(ipc)
add_subdirectory(overwolf)
add_subdirectory(client)
add_subdirectory(agent)

add_library(owh_cpp INTERFACE)
add_library(OverwolfHook::Cpp ALIAS owh_cpp)
set_target_properties(owh_cpp PROPERTIES EXPORT_NAME Cpp)
target_compile_features(owh_cpp INTERFACE cxx_std_20)
target_link_libraries(owh_cpp INTERFACE owh_client)
set(cpp_headers
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/overwolf_hook.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/detail/handle.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/sdk.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/overlays.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/wait.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/events.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/game_state.hpp"
    "${PROJECT_SOURCE_DIR}/include/overwolf_hook/overwolf.hpp")
target_sources(owh_cpp INTERFACE FILE_SET cpp_headers TYPE HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include" FILES ${cpp_headers})
install(TARGETS owh_abi owh_cpp EXPORT OverwolfHookTargets
    FILE_SET cpp_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# The payload is explicitly deployed with the Client; it is not a link
# dependency of the Client. Both files have one configuration-specific staging
# owner so parallel tools/tests never race while copying the same DLL.
set(OWH_NATIVE_STAGE "${PROJECT_BINARY_DIR}/native/$<CONFIG>" CACHE INTERNAL "Matching native DLL staging")
add_custom_command(
    OUTPUT "${OWH_NATIVE_STAGE}/owh_client.dll" "${OWH_NATIVE_STAGE}/owh_agent.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OWH_NATIVE_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:owh_client>" "${OWH_NATIVE_STAGE}/owh_client.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:owh_agent>" "${OWH_NATIVE_STAGE}/owh_agent.dll"
    DEPENDS owh_client owh_agent
    VERBATIM)
add_custom_target(owh_native_bundle ALL DEPENDS
    "${OWH_NATIVE_STAGE}/owh_client.dll" "${OWH_NATIVE_STAGE}/owh_agent.dll")
set_target_properties(owh_native_bundle PROPERTIES FOLDER Native)
