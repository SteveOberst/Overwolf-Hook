include(FetchContent)
# The archive identity is pinned independently of upstream CMake's version fields.
FetchContent_Declare(minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
    URL_HASH SHA256=172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
function(owh_add_minhook)
    set(BUILD_SHARED_LIBS OFF)
    # Scoped to the old upstream CMake declaration, never changed in its sources.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    FetchContent_MakeAvailable(minhook)
    # Statically linked into capture when enabled; don't export a second native
    # package or expose MinHook headers through OverwolfHook's consumer contract.
    set_property(DIRECTORY "${minhook_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
endfunction()
owh_add_minhook()
