set(sources
    "src/mutex.cpp"
    "src/identity.cpp"
    "src/sha256.cpp"
    "include/owh/platform/identity.hpp"
    "include/owh/platform/sha256.hpp"
    "include/owh/platform/deadline.hpp"
    "include/owh/platform/wait.hpp"
    "include/owh/platform/handle.hpp"
    "include/owh/platform/result.hpp"
    "include/owh/platform/mutex.hpp"
    "include/owh/platform/actor_signals.hpp")
list(APPEND sources "include/owh/platform/pe_image.hpp")
add_library(owh_platform STATIC ${sources})
owh_configure_native(owh_platform)
target_include_directories(owh_platform PUBLIC "include")
target_link_libraries(owh_platform PUBLIC owh_core PRIVATE bcrypt advapi32)
set_target_properties(owh_platform PROPERTIES FOLDER Native/Shared)
