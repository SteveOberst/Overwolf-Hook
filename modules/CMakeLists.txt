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

# Each product DLL is linked directly into its configuration's runtime directory.
set_target_properties(owh_client owh_agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OWH_NATIVE_DIRECTORY}")
