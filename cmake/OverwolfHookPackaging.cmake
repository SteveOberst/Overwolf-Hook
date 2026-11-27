install(EXPORT OverwolfHookTargets NAMESPACE OverwolfHook::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/OverwolfHookConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/OverwolfHookConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/OverwolfHookConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/OverwolfHookRuntime.cmake"
    "${PROJECT_BINARY_DIR}/OverwolfHookConfig.cmake" "${PROJECT_BINARY_DIR}/OverwolfHookConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OverwolfHook)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES "${CMAKE_CURRENT_LIST_DIR}/SDK-README.md"
    DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME README.md)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/MinHook.LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/third-party RENAME MinHook-LICENSE.txt)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/Boost.LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/third-party RENAME Boost-LICENSE.txt)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "overwolf-hook-${OWH_VERSION}-windows-x64")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VENDOR SteveOberst)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/SteveOberst/Overwolf-Hook")
include(CPack)
