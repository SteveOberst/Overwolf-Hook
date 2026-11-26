# A separate compilation is required: the seam changes private implementation.
add_library(owh_client_fixture STATIC ${client_sources}
    "${PROJECT_SOURCE_DIR}/tests/fixtures/client/src/host_fixture.cpp")
owh_configure_client_implementation(owh_client_fixture)
target_compile_definitions(owh_client_fixture
    PUBLIC OWH_CLIENT_OVERLAY_TEST_FIXTURE INTERFACE OWH_BUILD_owh_client)
target_include_directories(owh_client_fixture PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/fixtures/client/include")
target_link_libraries(owh_client_fixture PUBLIC owh_abi owh_platform owh_ipc owh_overwolf_passive)
set_target_properties(owh_client_fixture PROPERTIES FOLDER Tests/Fixtures)
