cmake_minimum_required(VERSION 3.25)
project(overwolf_hook_reader LANGUAGES C)
find_package(OverwolfHook CONFIG REQUIRED)
foreach(example IN ITEMS read_events read_state)
    add_executable(${example} ${example}.c)
    set_target_properties(${example} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_compile_definitions(${example} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(${example} PRIVATE OverwolfHook::Client)
    add_custom_command(TARGET ${example} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${example}> $<TARGET_FILE_DIR:${example}>
        COMMAND_EXPAND_LISTS)
endforeach()
