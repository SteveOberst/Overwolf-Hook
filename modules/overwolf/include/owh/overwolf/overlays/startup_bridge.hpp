#pragma once
#include <windows.h>

namespace overwolf_hook::overwolf {
/** Called synchronously after OWServer resolves runAgent, before the pointer is
 * returned to it. No rendering or object access happens in the bridge itself. */
using StartupLookup = void (*)(HMODULE agent, FARPROC run_agent) noexcept;

/** Install a permanent observer in OWServer's named GetProcAddress IAT entry.
 * Requires OWAgent absent both before and after installation. A missed startup
 * or a competing IAT patch fails closed; no hot-attach fallback is performed. */
bool arm_startup_bridge(HMODULE server, StartupLookup) noexcept;
} // namespace overwolf_hook::overwolf
