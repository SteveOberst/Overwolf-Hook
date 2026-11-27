#pragma once
#include "overwolf/process.hpp"

#include <overwolf_hook/types/common.h>
#include <owh/platform/handle.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace overwolf_hook::client::detail {
struct Module {
    uintptr_t base = 0;
    std::wstring path;
};

Module module_in_process(uint32_t pid, std::wstring_view wanted);
std::wstring client_module_directory();
owh_result bootstrap_agent(overwolf::ProcessIdentity &host, uint64_t client_started, uint32_t attach_mode);
} // namespace overwolf_hook::client::detail
