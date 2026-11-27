#pragma once
#include <owh/platform/handle.hpp>
#include <owh/overwolf/host_process.hpp>
#include <overwolf_hook/types/overwolf.h>
#include <string>
#include <utility>

namespace overwolf_hook::client::host {
owh_result installation(owh_overwolf_installation &) noexcept;
owh_result process(uint32_t pid, uint64_t started, owh_overwolf_process &) noexcept;
owh_result start(uint32_t timeout_ms, owh_overwolf_process &) noexcept;
owh_result stop(uint32_t pid, uint64_t started, uint32_t timeout_ms) noexcept;

// Native strings are converted only at the public C value boundary.
std::wstring file_version(const std::wstring &path);
void copy_utf8(char *output, size_t capacity, const std::wstring &text);
void describe(const overwolf::ProcessIdentity &, owh_overwolf_process &);
} // namespace overwolf_hook::client::host
