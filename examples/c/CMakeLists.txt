cmake_minimum_required(VERSION 3.25)
project(overwolf_event_hook_reader LANGUAGES C)
find_package(OverwolfEventHook CONFIG REQUIRED)
add_executable(read_events read_events.c)
set_target_properties(read_events PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_compile_definitions(read_events PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(read_events PRIVATE OverwolfEventHook::Client)
add_custom_command(TARGET read_events POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:read_events> $<TARGET_FILE_DIR:read_events>
    COMMAND_EXPAND_LISTS)
