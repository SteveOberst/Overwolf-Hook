set(passive_sources
    src/host_process.cpp
    include/owh/overwolf/host_process.hpp
    src/state/info_db.cpp
    src/state/storage.cpp
    src/state/sources.cpp
    src/state/provider_sources.cpp
    include/owh/overwolf/provider_sources.hpp
    include/owh/overwolf/info_db.hpp
    include/owh/overwolf/state_storage.hpp
    src/signatures/resolver.cpp
    src/signatures/event_resolver.cpp
    include/owh/overwolf/signatures/resolver.hpp
    include/owh/overwolf/signatures/overlay_targets.hpp
    include/owh/overwolf/signatures/lifecycle_targets.hpp
    include/owh/overwolf/signatures/event_resolver.hpp
    include/owh/overwolf/signatures/event_targets.hpp
    include/owh/overwolf/event_channels.hpp)
add_library(owh_overwolf_passive STATIC ${passive_sources})
owh_configure_native(owh_overwolf_passive)
target_include_directories(owh_overwolf_passive PUBLIC include)
target_link_libraries(owh_overwolf_passive PUBLIC owh_platform owh_core)
set_target_properties(owh_overwolf_passive PROPERTIES FOLDER Native/Overwolf)

set(active_sources
    src/overlays/lifetime_admission.cpp
    src/overlays/startup_bridge.cpp
    src/overlays/overlay_host.cpp
    src/overlays/overlay_executor.cpp
    src/overlays/source_adapter.cpp
    src/overlays/d3d11_raw_resource.cpp
    src/events/source.cpp
    include/owh/overwolf/overlays/lifetime_admission.hpp
    include/owh/overwolf/overlays/startup_bridge.hpp
    include/owh/overwolf/overlays/overlay_host.hpp
    include/owh/overwolf/overlays/overlay_executor.hpp
    include/owh/overwolf/overlays/source_adapter.hpp
    include/owh/overwolf/overlays/d3d11_raw_resource.hpp
    include/owh/overwolf/overlays/native_contract.hpp
    include/owh/overwolf/overlays/pixel_view.hpp)
list(APPEND active_sources include/owh/overwolf/event_source.hpp)
add_library(owh_overwolf_active STATIC ${active_sources})
owh_configure_native(owh_overwolf_active)
target_include_directories(owh_overwolf_active PUBLIC include)
target_link_libraries(owh_overwolf_active PUBLIC owh_overwolf_passive owh_core owh_platform
    PRIVATE owh_build_info minhook bcrypt d3d11 dxgi)
set_target_properties(owh_overwolf_active PROPERTIES FOLDER Native/Overwolf)
if(BUILD_TESTING)
    add_library(owh_overwolf_fixture STATIC ${active_sources})
    owh_configure_native(owh_overwolf_fixture)
    target_compile_definitions(owh_overwolf_fixture PUBLIC OWH_ADMISSION_TEST_FIXTURE)
    target_include_directories(owh_overwolf_fixture PUBLIC include)
    target_link_libraries(owh_overwolf_fixture PUBLIC owh_overwolf_passive owh_core owh_platform
        PRIVATE owh_build_info minhook bcrypt d3d11 dxgi)
    set_target_properties(owh_overwolf_fixture PROPERTIES FOLDER Tests/Fixtures)
endif()
