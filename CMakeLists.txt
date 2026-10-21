cmake_minimum_required(VERSION 3.25)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" OWH_VERSION LIMIT_COUNT 1)
set(OWH_RELEASE_VERSION "${OWH_VERSION}")
string(REGEX MATCH "^[0-9]+[.][0-9]+[.][0-9]+" OWH_PROJECT_VERSION "${OWH_RELEASE_VERSION}")
project(overwolf_hook VERSION ${OWH_PROJECT_VERSION} LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Overwolf-Hook supports Windows x64 only")
endif()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(OWH_HOOK_TESTS "Build MinHook experiments against the owned fixture" ON)
option(OWH_FFM_TESTS "Exercise the C ABI from Java 25 FFM" OFF)
option(OWH_JVM_BINDINGS "Build and test the bundled Java 25 API with jextract-generated bindings" OFF)
set(OWH_JEXTRACT "" CACHE FILEPATH "Optional local jextract 25 executable instead of the pinned download")
option(OWH_OVERWOLF_PROBE "Build the reviewed, explicit Overwolf capture experiment" ON)
set(OWH_REVIEWED_CONSUMER "" CACHE FILEPATH "Optional exact consumer DLL for the owned-host ABI test")
if(OWH_OVERWOLF_PROBE OR (BUILD_TESTING AND OWH_HOOK_TESTS))
    include(cmake/MinHook.cmake)
endif()

add_library(owh_transport STATIC src/transport/channel.cpp)
target_compile_features(owh_transport PUBLIC cxx_std_20)
target_include_directories(owh_transport PUBLIC include PRIVATE src)

add_library(owh_capture SHARED src/capture/capture.cpp)
add_library(OverwolfHook::Capture ALIAS owh_capture)
add_library(owh_client SHARED src/client/client.cpp src/client/state.cpp src/client/discovery.cpp src/state/infodb.cpp)
add_library(OverwolfHook::Client ALIAS owh_client)
foreach(target IN ITEMS owh_capture owh_client)
    target_link_libraries(${target} PRIVATE owh_transport)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> PRIVATE src)
    target_compile_definitions(${target} PRIVATE OWH_BUILD_${target})
    set_target_properties(${target} PROPERTIES PREFIX "")
endforeach()
set_target_properties(owh_capture PROPERTIES EXPORT_NAME Capture)
set_target_properties(owh_client PROPERTIES EXPORT_NAME Client)
add_executable(owh_info tools/info.cpp)
target_compile_features(owh_info PRIVATE cxx_std_20)
target_compile_definitions(owh_info PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(owh_info PRIVATE owh_client)
install(TARGETS owh_info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(OWH_OVERWOLF_PROBE)
    target_sources(owh_capture PRIVATE src/capture/overwolf/probe.cpp src/capture/overwolf/reader.cpp)
    target_link_libraries(owh_capture PRIVATE minhook bcrypt)
    add_executable(owh_probe tools/probe.cpp)
    target_compile_features(owh_probe PRIVATE cxx_std_20)
    target_compile_definitions(owh_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(owh_probe PRIVATE owh_client)
    add_dependencies(owh_probe owh_capture)
    install(TARGETS owh_probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES third_party/MinHook.LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/third-party RENAME MinHook-LICENSE.txt)
endif()

if(OWH_JVM_BINDINGS)
    if(NOT OWH_OVERWOLF_PROBE)
        message(FATAL_ERROR "The bundled JVM capture API requires OWH_OVERWOLF_PROBE")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Java 25 REQUIRED COMPONENTS Runtime Development)
    get_filename_component(_java_bin "${Java_JAVA_EXECUTABLE}" DIRECTORY)
    get_filename_component(_java_home "${_java_bin}" DIRECTORY)
    set(_jvm_output "${CMAKE_CURRENT_BINARY_DIR}/jvm/$<CONFIG>")
    set(_jvm_jar "${_jvm_output}/overwolf-hook-${OWH_RELEASE_VERSION}-windows-x64.jar")
    set(_jextract_args)
    if(OWH_JEXTRACT)
        list(APPEND _jextract_args --jextract "${OWH_JEXTRACT}")
    endif()
    file(GLOB_RECURSE _jvm_sources CONFIGURE_DEPENDS jvm/src/*.java examples/java/*.java include/overwolf_hook/*.h)
    add_custom_command(OUTPUT "${_jvm_jar}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_jvm.py"
            --native-dir "$<TARGET_FILE_DIR:owh_client>" --output "${_jvm_output}"
            --java-home "${_java_home}" ${_jextract_args}
        DEPENDS owh_client owh_capture owh_probe owh_info ${_jvm_sources}
            tools/build_jvm.py jvm/toolchain.json version.txt
        VERBATIM)
    add_custom_target(owh_jvm ALL DEPENDS "${_jvm_jar}")
    install(FILES "${_jvm_jar}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
    if(BUILD_TESTING)
        add_test(NAME jvm.artifact COMMAND "${Java_JAVA_EXECUTABLE}" --enable-native-access=ALL-UNNAMED
            --class-path "${_jvm_jar};${_jvm_output}/test-classes" JvmSmokeTest transport "$<TARGET_FILE:owh_capture>")
        set_tests_properties(jvm.artifact PROPERTIES TIMEOUT 30)
        add_test(NAME jvm.cache COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_cache_test.py
            "${Java_JAVA_EXECUTABLE}" "${_jvm_jar};${_jvm_output}/test-classes" "$<TARGET_FILE:owh_capture>")
        set_tests_properties(jvm.cache PROPERTIES TIMEOUT 30)
    endif()
endif()

foreach(target IN ITEMS owh_transport owh_capture owh_client owh_info owh_probe)
    if(NOT TARGET ${target})
        continue()
    endif()
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
endforeach()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME info_recording COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/info_recorder_test.py $<TARGET_FILE:owh_info>)
    set_tests_properties(info_recording PROPERTIES TIMEOUT 30)
    add_test(NAME capture_inspection COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_capture_test.py)
    set_tests_properties(capture_inspection PROPERTIES TIMEOUT 15)
    add_executable(owh_discovery_tests tests/discovery_tests.cpp)
    target_compile_features(owh_discovery_tests PRIVATE cxx_std_20)
    target_compile_definitions(owh_discovery_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(owh_discovery_tests PRIVATE owh_client)
    add_test(NAME channel_discovery COMMAND owh_discovery_tests)
    set_tests_properties(channel_discovery PROPERTIES TIMEOUT 15)
    add_executable(owh_controller_tests tests/controller_tests.cpp)
    target_compile_features(owh_controller_tests PRIVATE cxx_std_20)
    target_compile_definitions(owh_controller_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME controller_stopping COMMAND owh_controller_tests)
    set_tests_properties(controller_stopping PROPERTIES TIMEOUT 15)
    add_executable(owh_state_tests tests/state_tests.cpp)
    target_compile_features(owh_state_tests PRIVATE cxx_std_20)
    target_compile_definitions(owh_state_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(owh_state_tests PRIVATE owh_client)
    if(OWH_JVM_BINDINGS)
        add_test(NAME jvm.state COMMAND owh_state_tests jvm-library "${Java_JAVA_EXECUTABLE}"
            "${_jvm_jar};${_jvm_output}/test-classes")
        set_tests_properties(jvm.state PROPERTIES TIMEOUT 20)
    endif()
    foreach(case IN ITEMS roundtrip bounds availability lifecycle contention)
        add_test(NAME state.${case} COMMAND owh_state_tests ${case})
        set_tests_properties(state.${case} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(owh_transport_tests tests/transport_tests.cpp)
    target_compile_features(owh_transport_tests PRIVATE cxx_std_20)
    target_link_libraries(owh_transport_tests PRIVATE owh_capture owh_client)
    target_compile_definitions(owh_transport_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    foreach(case IN ITEMS roundtrip readers boundary overflow buffer invalid lifecycle process crash corrupt)
        add_test(NAME transport.${case} COMMAND owh_transport_tests ${case})
        set_tests_properties(transport.${case} PROPERTIES TIMEOUT 15)
    endforeach()

    if(OWH_FFM_TESTS)
        find_package(Java 25 REQUIRED COMPONENTS Runtime)
        add_test(NAME state.java_ffm COMMAND owh_state_tests jvm ${Java_JAVA_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/java/FfmStateRead.java $<TARGET_FILE:owh_client>)
        set_tests_properties(state.java_ffm PROPERTIES TIMEOUT 20
            PASS_REGULAR_EXPRESSION "player.unknown_key ticks=123456789 hex=ff008042")
        add_test(NAME discovery.java_ffm COMMAND owh_state_tests jvm ${Java_JAVA_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/java/FfmChannels.java $<TARGET_FILE:owh_client>)
        set_tests_properties(discovery.java_ffm PROPERTIES TIMEOUT 20)
        add_test(NAME java_ffm COMMAND ${Java_JAVA_EXECUTABLE}
            --enable-native-access=ALL-UNNAMED ${CMAKE_CURRENT_SOURCE_DIR}/examples/java/FfmRoundtrip.java
            $<TARGET_FILE:owh_capture> $<TARGET_FILE:owh_client>)
        set_tests_properties(java_ffm PROPERTIES TIMEOUT 30)
    endif()

    if(OWH_HOOK_TESTS)
        add_library(owh_hook_fixture SHARED tests/hook_fixture.cpp)
        if(MSVC)
            target_compile_options(owh_hook_fixture PRIVATE /Od /Ob0 /GL-)
        endif()
        add_executable(owh_hook_tests tests/hook_tests.cpp)
        target_compile_features(owh_hook_tests PRIVATE cxx_std_20)
        target_compile_definitions(owh_hook_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(owh_hook_tests PRIVATE minhook owh_capture owh_client)
        add_dependencies(owh_hook_tests owh_hook_fixture)
        add_test(NAME hook_trampoline COMMAND owh_hook_tests $<TARGET_FILE:owh_hook_fixture>)
        set_tests_properties(hook_trampoline PROPERTIES TIMEOUT 15)
    endif()
    if(OWH_OVERWOLF_PROBE AND OWH_REVIEWED_CONSUMER)
        add_executable(owh_consumer_host tests/consumer_host.cpp src/capture/overwolf/reader.cpp)
        target_compile_features(owh_consumer_host PRIVATE cxx_std_20)
        target_compile_definitions(owh_consumer_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_include_directories(owh_consumer_host PRIVATE src)
        target_link_libraries(owh_consumer_host PRIVATE owh_capture owh_client)
        add_test(NAME reviewed_consumer_hook COMMAND owh_consumer_host "${OWH_REVIEWED_CONSUMER}")
        set_tests_properties(reviewed_consumer_hook PROPERTIES TIMEOUT 20)
        add_executable(owh_controller_host tests/consumer_host.cpp src/capture/overwolf/reader.cpp)
        target_compile_features(owh_controller_host PRIVATE cxx_std_20)
        target_compile_definitions(owh_controller_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_include_directories(owh_controller_host PRIVATE src)
        target_link_libraries(owh_controller_host PRIVATE owh_capture owh_client)
        set_target_properties(owh_controller_host PROPERTIES OUTPUT_NAME Overwolf
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture")
        add_custom_command(TARGET owh_controller_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:owh_controller_host> $<TARGET_FILE_DIR:owh_controller_host>
            COMMAND_EXPAND_LISTS)
        add_test(NAME reviewed_event_recording COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/event_recorder_test.py $<TARGET_FILE:owh_probe>
            $<TARGET_FILE:owh_controller_host> "${OWH_REVIEWED_CONSUMER}" $<TARGET_FILE:owh_capture>)
        set_tests_properties(reviewed_event_recording PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME unreviewed_consumer_recording COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/event_recorder_test.py $<TARGET_FILE:owh_probe>
            $<TARGET_FILE:owh_controller_host> "${OWH_REVIEWED_CONSUMER}" $<TARGET_FILE:owh_capture> --unreviewed)
        set_tests_properties(unreviewed_consumer_recording PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        if(OWH_JVM_BINDINGS)
            add_test(NAME reviewed_jvm_capture COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/jvm_capture_test.py $<TARGET_FILE:owh_controller_host>
                "${OWH_REVIEWED_CONSUMER}" "${Java_JAVA_EXECUTABLE}"
                "${_jvm_jar};${_jvm_output}/test-classes")
            set_tests_properties(reviewed_jvm_capture PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        endif()
    endif()
    if(OWH_OVERWOLF_PROBE)
        add_executable(owh_reader_tests tests/reader_tests.cpp src/capture/overwolf/reader.cpp)
        target_compile_features(owh_reader_tests PRIVATE cxx_std_20)
        target_compile_definitions(owh_reader_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_include_directories(owh_reader_tests PRIVATE src include)
        add_test(NAME reader_signature COMMAND owh_reader_tests)
        add_executable(owh_channel_tests tests/channel_tests.cpp)
        target_compile_features(owh_channel_tests PRIVATE cxx_std_20)
        target_include_directories(owh_channel_tests PRIVATE src)
        add_test(NAME event_channels COMMAND owh_channel_tests)
    endif()
endif()

install(TARGETS owh_capture owh_client EXPORT OverwolfHookTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/overwolf_hook/bridge.h include/overwolf_hook/state.h include/overwolf_hook/discovery.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/overwolf_hook)
if(OWH_OVERWOLF_PROBE)
    install(FILES include/overwolf_hook/probe.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/overwolf_hook)
endif()
install(EXPORT OverwolfHookTargets NAMESPACE OverwolfHook:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
configure_package_config_file(cmake/OverwolfHookConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/OverwolfHookConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/OverwolfHookConfigVersion.cmake" "set(PACKAGE_VERSION \"${OWH_RELEASE_VERSION}\")\nset(PACKAGE_VERSION_COMPATIBLE FALSE)\nset(PACKAGE_VERSION_EXACT FALSE)\nif(DEFINED CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P STREQUAL \"${CMAKE_SIZEOF_VOID_P}\")\n set(PACKAGE_VERSION_UNSUITABLE TRUE)\nendif()\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/OverwolfHookConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/OverwolfHookConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "overwolf-hook-${OWH_RELEASE_VERSION}-windows-x64")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VENDOR "SteveOberst")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/SteveOberst/Overwolf-Hook")
include(CPack)
