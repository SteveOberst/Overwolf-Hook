#pragma once

#include <windows.h>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <cstdint>
#include <owh/platform/deadline.hpp>
#include <owh/platform/wait.hpp>

namespace overwolf_hook::ipc {
enum class SharedLockStatus : unsigned char { locked, busy, cancelled, peer_gone, corrupt };

/** Bounded, allocation-free acquisition for process-shared Boost mutexes.
 * Retries use a monotonic deadline and short waits so peer death/cancellation
 * remain observable while a live peer is paused.
 */
class SharedLock final {
  public:
    SharedLock(boost::interprocess::interprocess_mutex &mutex, uint32_t timeout_ms,
               HANDLE peer = nullptr, HANDLE cancel = nullptr) noexcept
        : lock_(mutex, boost::interprocess::defer_lock) {
        const Deadline deadline(timeout_ms);
        for (;;) {
            try {
                if (lock_.try_lock()) {
                    state_ = SharedLockStatus::locked;
                    return;
                }
            } catch (const boost::interprocess::interprocess_exception &) {
                state_ = SharedLockStatus::corrupt;
                return;
            } catch (...) {
                state_ = SharedLockStatus::corrupt;
                return;
            }
            if (cancel && signaled(cancel)) {
                state_ = SharedLockStatus::cancelled;
                return;
            }
            if (peer && signaled(peer)) {
                state_ = SharedLockStatus::peer_gone;
                return;
            }
            if (deadline.expired()) {
                state_ = SharedLockStatus::busy;
                return;
            }
            const DWORD remaining = deadline.remaining_ms();
            const DWORD pause = remaining == INFINITE ? 10 : (remaining < 10 ? remaining : 10);
            HANDLE handles[2]{};
            DWORD count = 0;
            if (cancel)
                handles[count++] = cancel;
            if (peer)
                handles[count++] = peer;
            if (count) {
                const auto waited = wait_any(handles, count, pause);
                if (waited.kind == WaitKind::failed || waited.kind == WaitKind::abandoned) {
                    state_ = SharedLockStatus::corrupt;
                    return;
                }
            } else {
                Sleep(pause);
            }
        }
    }
    SharedLock(const SharedLock &) = delete;
    SharedLock &operator=(const SharedLock &) = delete;
    SharedLockStatus status() const noexcept { return state_; }
    void unlock() noexcept {
        if (state_ != SharedLockStatus::locked)
            return;
        try {
            lock_.unlock();
        } catch (...) {
            state_ = SharedLockStatus::corrupt;
            return;
        }
        state_ = SharedLockStatus::busy;
    }

  private:
    static bool signaled(HANDLE handle) noexcept {
        return wait_one(handle, 0).kind == WaitKind::signaled;
    }
    using NativeLock = boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>;
    NativeLock lock_;
    SharedLockStatus state_ = SharedLockStatus::busy;
};
} // namespace overwolf_hook::ipc
