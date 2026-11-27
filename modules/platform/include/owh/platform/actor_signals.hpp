#pragma once
#include <new>
#include <owh/platform/handle.hpp>

namespace overwolf_hook::platform {
/** Owned before actor startup, including every partially constructed failure
 * path. */
struct ActorSignals final {
    using CreateEventFn = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
    Handle command;
    Handle stop;
    Handle ready;
    Handle source;

    explicit ActorSignals(CreateEventFn create = &::CreateEventW)
        : command(create(nullptr, TRUE, FALSE, nullptr)),
          stop(create(nullptr, TRUE, FALSE, nullptr)), ready(create(nullptr, TRUE, FALSE, nullptr)),
          source(create(nullptr, FALSE, FALSE, nullptr)) {
        if (!command || !stop || !ready || !source)
            throw std::bad_alloc();
    }
};
} // namespace overwolf_hook::platform
