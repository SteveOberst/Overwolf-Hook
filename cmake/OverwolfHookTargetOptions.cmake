include_guard(GLOBAL)

function(owh_configure_native target)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX
            "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
            "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
    endif()
endfunction()
