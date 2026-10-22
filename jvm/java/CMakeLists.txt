set(OWH_JEXTRACT "" CACHE FILEPATH "Optional local jextract 25 executable instead of the pinned download")
if(NOT OWH_OVERWOLF_PROBE)
    message(FATAL_ERROR "The bundled JVM capture API requires OWH_OVERWOLF_PROBE")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Java 25 REQUIRED COMPONENTS Runtime Development)
get_filename_component(_java_bin "${Java_JAVA_EXECUTABLE}" DIRECTORY)
get_filename_component(_java_home "${_java_bin}" DIRECTORY)
set(_java_output "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
set(_java_jar "${_java_output}/overwolf-hook-${OWH_RELEASE_VERSION}-windows-x64.jar")
set(_jextract_args)
if(OWH_JEXTRACT)
    list(APPEND _jextract_args --jextract "${OWH_JEXTRACT}")
endif()
file(GLOB_RECURSE _java_sources CONFIGURE_DEPENDS src/*.java "${PROJECT_SOURCE_DIR}/examples/java/*.java" "${PROJECT_SOURCE_DIR}/include/overwolf_hook/*.h")
add_custom_command(OUTPUT "${_java_jar}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/build.py"
        --native-dir "$<TARGET_FILE_DIR:owh_client>" --output "${_java_output}"
        --java-home "${_java_home}" ${_jextract_args}
    DEPENDS owh_client owh_capture owh_probe owh_info ${_java_sources}
        build.py toolchain.json "${PROJECT_SOURCE_DIR}/version.txt" "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${PROJECT_SOURCE_DIR}/third_party/MinHook.LICENSE.txt"
    VERBATIM)
add_custom_target(owh_java ALL DEPENDS "${_java_jar}")
# Native fixture tests can execute the built SDK without owning its layout.
set_property(TARGET owh_java PROPERTY OWH_TEST_CLASSPATH "${_java_jar};${_java_output}/test-classes")
install(FILES "${_java_jar}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(BUILD_TESTING)
    add_test(NAME jvm.artifact COMMAND "${Java_JAVA_EXECUTABLE}" --enable-native-access=ALL-UNNAMED
        --class-path "${_java_jar};${_java_output}/test-classes" JvmSmokeTest transport "$<TARGET_FILE:owh_capture>")
    set_tests_properties(jvm.artifact PROPERTIES TIMEOUT 30)
    add_test(NAME jvm.cache COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/jvm_cache_test.py
        "${Java_JAVA_EXECUTABLE}" "${_java_jar};${_java_output}/test-classes" "$<TARGET_FILE:owh_capture>")
    set_tests_properties(jvm.cache PROPERTIES TIMEOUT 30)
endif()
