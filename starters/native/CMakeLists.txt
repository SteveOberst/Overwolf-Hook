cmake_minimum_required(VERSION 3.25)
project(overwolf_first_overlay LANGUAGES C)
set(OVERWOLF_HOOK_VERSION "" CACHE STRING "Release version from the project's latest release badge")
find_package(OverwolfHook CONFIG QUIET)
if(NOT OverwolfHook_FOUND)
    if(NOT OVERWOLF_HOOK_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "Set OVERWOLF_HOOK_VERSION to the release version, or supply OverwolfHook_DIR for an installed SDK")
    endif()
    include(FetchContent)
    FetchContent_Declare(overwolf_hook_sdk
        URL "https://github.com/SteveOberst/Overwolf-Hook/releases/download/v${OVERWOLF_HOOK_VERSION}/overwolf-hook-${OVERWOLF_HOOK_VERSION}-abi-windows-x64.zip")
    FetchContent_MakeAvailable(overwolf_hook_sdk)
    find_package(OverwolfHook ${OVERWOLF_HOOK_VERSION} EXACT CONFIG REQUIRED
        PATHS "${overwolf_hook_sdk_SOURCE_DIR}" NO_DEFAULT_PATH)
endif()
add_executable(first_overlay main.c)
target_compile_features(first_overlay PRIVATE c_std_17)
target_compile_definitions(first_overlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(first_overlay PRIVATE OverwolfHook::Client)
add_custom_command(TARGET first_overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OverwolfHook::Client>" "$<TARGET_FILE:OverwolfHook::Agent>"
        "$<TARGET_FILE_DIR:first_overlay>" VERBATIM)
