include_guard(GLOBAL)

set(OWH_BUILD_NUMBER "0" CACHE STRING "Unsigned SDK build number (CI uses GITHUB_RUN_ID)")
if(NOT OWH_BUILD_NUMBER MATCHES "^(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "OWH_BUILD_NUMBER must be a nonnegative uint64 decimal value")
endif()
string(LENGTH "${OWH_BUILD_NUMBER}" build_number_length)
if(build_number_length GREATER 20)
    message(FATAL_ERROR "OWH_BUILD_NUMBER exceeds uint64_t")
elseif(build_number_length EQUAL 20 AND "${OWH_BUILD_NUMBER}" STRGREATER "18446744073709551615")
    message(FATAL_ERROR "OWH_BUILD_NUMBER exceeds uint64_t")
endif()
string(LENGTH "${OWH_VERSION}" version_length)
if(version_length GREATER_EQUAL 128)
    message(FATAL_ERROR "version.txt SDK version must fit OWH_SDK_VERSION_CAPACITY")
endif()

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_LIST_DIR}/BuildInfo.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/BuildInfo.hpp" @ONLY)
enable_language(RC)
configure_file("${CMAKE_CURRENT_LIST_DIR}/NativeVersion.rc.in"
    "${PROJECT_BINARY_DIR}/generated/NativeVersion.rc" @ONLY)
set(OWH_VERSION_RESOURCE "${PROJECT_BINARY_DIR}/generated/NativeVersion.rc")

add_library(owh_build_info INTERFACE)
target_include_directories(owh_build_info INTERFACE "${PROJECT_BINARY_DIR}/generated")
