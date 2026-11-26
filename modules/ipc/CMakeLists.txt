set(sources
    "src/services.cpp"
    "include/owh/ipc/session_protocol.hpp"
    "include/owh/ipc/services.hpp"
    "include/owh/ipc/wire_contract.hpp"
    "src/shared_region.cpp"
    "src/session_transport.cpp"
    "include/owh/ipc/shared_region.hpp"
    "include/owh/ipc/shared_lock.hpp"
    "include/owh/ipc/session_transport.hpp"
    "src/event_log.cpp"
    "src/event_cursor.cpp"
    "src/pixel_stream.cpp"
    "include/owh/ipc/event_log.hpp"
    "include/owh/ipc/pixel_stream.hpp")
add_library(owh_ipc STATIC ${sources})
owh_configure_native(owh_ipc)
target_include_directories(owh_ipc PUBLIC "include")
target_link_libraries(owh_ipc PUBLIC owh_platform owh_core owh_boost_interprocess)
set_target_properties(owh_ipc PROPERTIES FOLDER Native/IPC)
