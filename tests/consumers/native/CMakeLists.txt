cmake_minimum_required(VERSION 3.25)
project(owh_native_consumer_fixture LANGUAGES C CXX)
include(CTest)
find_package(OverwolfHook CONFIG REQUIRED)
foreach(language IN ITEMS c cpp)
    add_executable(consumer_${language} consumer.${language})
    target_compile_features(consumer_${language} PRIVATE c_std_17 cxx_std_20)
    target_compile_definitions(consumer_${language} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(language STREQUAL "cpp")
        target_link_libraries(consumer_${language} PRIVATE OverwolfHook::Cpp)
    else()
        target_link_libraries(consumer_${language} PRIVATE OverwolfHook::Client)
    endif()
    add_custom_command(TARGET consumer_${language} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OverwolfHook::Client>
            $<TARGET_FILE:OverwolfHook::Agent>
            $<TARGET_FILE_DIR:consumer_${language}>
        VERBATIM)
    add_test(NAME sdk_consumer.${language} COMMAND consumer_${language})
endforeach()
