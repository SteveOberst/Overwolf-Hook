include_guard(GLOBAL)

# Deploy the consumed SDK's DLL pair. Agent is a payload, not a link dependency.
function(overwolf_hook_deploy_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:OverwolfHook::Client>"
            "$<TARGET_FILE:OverwolfHook::Agent>"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
endfunction()
