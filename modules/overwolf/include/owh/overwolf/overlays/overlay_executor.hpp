#pragma once

#include <owh/platform/actor_signals.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace overwolf_hook::overwolf {

/** Single-threaded message executor for overlay operations. It owns its thread,
 * wake signals and command queue; callers may start it only for active sources.
 */
class OverlayExecutor final {
  public:
    enum class State : uint8_t { starting, running, stopping, failed, stopped };
    struct Callbacks {
        std::function<bool()> start;
        std::function<void()> messages;
        std::function<void()> sources;
        std::function<bool()> retry_pending;
        std::function<void()> stop;
    };

    OverlayExecutor() = default;
    ~OverlayExecutor();
    OverlayExecutor(const OverlayExecutor &) = delete;
    OverlayExecutor &operator=(const OverlayExecutor &) = delete;

    bool ensure_started(Callbacks callbacks) noexcept;
    void shutdown() noexcept;
    void signal_sources() noexcept;
    bool running() const noexcept {
        return state_.load(std::memory_order_acquire) == State::running;
    }
    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    size_t pending_commands() noexcept {
        std::lock_guard lock(queue_mutex_);
        return commands_.size();
    }

    template <class F> auto invoke(F &&function) -> std::invoke_result_t<F> {
        using R = std::invoke_result_t<F>;
        if (state_.load(std::memory_order_acquire) == State::running &&
            std::this_thread::get_id() == actor_thread_)
            return std::invoke(std::forward<F>(function));
        // Startup and shutdown own the thread object. Admit a command under the
        // same gate, then release it before waiting for worker progress.
        std::unique_lock lifecycle_lock(lifecycle_mutex_);
        if (!thread_.joinable() && state_.load(std::memory_order_acquire) == State::starting)
            return std::invoke(std::forward<F>(function));
        if (!thread_.joinable())
            throw std::runtime_error("overlay executor stopped");

        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        {
            std::lock_guard lock(queue_mutex_);
            if (state_.load(std::memory_order_acquire) != State::running)
                throw std::runtime_error("overlay executor stopped");
            commands_.push(PendingCommand{[fn = std::forward<F>(function), promise]() mutable {
                                              try {
                                                  if constexpr (std::is_void_v<R>) {
                                                      fn();
                                                      promise->set_value();
                                                  } else
                                                      promise->set_value(fn());
                                              } catch (...) {
                                                  promise->set_exception(std::current_exception());
                                              }
                                          },
                                          [promise](std::exception_ptr error) noexcept {
                                              try {
                                                  promise->set_exception(error);
                                              } catch (...) {
                                              }
                                          }});
        }
        SetEvent(signals_.command.get());
        lifecycle_lock.unlock();
        return future.get();
    }

  private:
    struct PendingCommand {
        std::function<void()> run;
        std::function<void(std::exception_ptr)> fail;
    };

    void run() noexcept;
    void run_loop();
    void fail_pending(State, std::exception_ptr) noexcept;
    void process_commands() noexcept;
    std::thread::id actor_thread_{};

    overwolf_hook::platform::ActorSignals signals_;
    std::thread thread_;
    mutable std::mutex lifecycle_mutex_;
    std::mutex queue_mutex_;
    std::queue<PendingCommand> commands_;
    std::atomic<State> state_{State::starting};
    Callbacks callbacks_;
};

} // namespace overwolf_hook::overwolf
