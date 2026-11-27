#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <span>

namespace overwolf_hook::overwolf::signatures::events {
// Inspect a copied PE image, including its executable sections and unwind table.
// Reject absent/ambiguous signatures and changed entry bytes before hooking.
size_t event_dispatcher_offset(std::span<const uint8_t> image);
void *resolve_event_dispatcher(HMODULE module);
} // namespace overwolf_hook::overwolf::signatures::events
