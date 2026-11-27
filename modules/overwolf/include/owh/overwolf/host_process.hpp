#pragma once
#include <owh/platform/handle.hpp>
#include <overwolf_hook/types/common.h>
#include <string>
#include <utility>

namespace overwolf_hook::overwolf {
enum class ProcessAccess { inspect, read_memory, inject };
struct ProcessIdentity {
    Handle process;
    uint32_t pid = 0;
    DWORD session = 0;
    uint64_t started = 0;
    std::wstring image;
};
/** Select the unique requested Overwolf main process in this user's session.
 * The executable name is a candidate filter; the x64 main-package role requires
 * sibling Client.Core and OWServer modules, independent of version/hash and of
 * whether those modules have loaded yet. This is role identification, not trust
 * or signature authentication.
 * Additional access is opened only after identity checks; never selects a game. */
std::pair<owh_result, ProcessIdentity> select_process(
    uint32_t requested_pid = 0, uint64_t expected_started = 0,
    ProcessAccess access = ProcessAccess::inspect);
} // namespace overwolf_hook::overwolf
