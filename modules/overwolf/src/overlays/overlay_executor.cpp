#include <owh/overwolf/overlays/overlay_executor.hpp>

#include <array>

namespace overwolf_hook::overwolf {
namespace {
std::exception_ptr stopped_error() noexcept {
    try {
        throw std::runtime_error("overlay executor stopped");
    } catch (...) {
        return std::current_exception();
    }
}
} // namespace

OverlayExecutor::~OverlayExecutor() { shutdown(); }

void OverlayExecutor::shutdown() noexcept {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (!thread_.joinable()) {
        std::lock_guard queue_lock(queue_mutex_);
        if (state_.load(std::memory_order_acquire) == State::starting)
            state_.store(State::stopped, std::memory_order_release);
        return;
    }
    {
        std::lock_guard queue_lock(queue_mutex_);
        if (state_.load(std::memory_order_acquire) == State::running) {
            if (signals_.stop)
                SetEvent(signals_.stop.get());
            state_.store(State::stopping, std::memory_order_release);
        }
    }
    if (signals_.stop)
        SetEvent(signals_.stop.get());
    if (signals_.command)
        SetEvent(signals_.command.get());
    if (thread_.joinable())
        thread_.join();
}

bool OverlayExecutor::ensure_started(Callbacks callbacks) noexcept {
    std::lock_guard lock(lifecycle_mutex_);
    const auto current = state_.load(std::memory_order_acquire);
    if (current == State::running)
        return true;
    if (current != State::starting)
        return false;
    try {
        callbacks_ = std::move(callbacks);
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        state_.store(State::failed, std::memory_order_release);
        return false;
    }
    const DWORD ready = WaitForSingleObject(signals_.ready.get(), 10000);
    if (ready != WAIT_OBJECT_0 || state_.load(std::memory_order_acquire) != State::running) {
        state_.store(State::failed, std::memory_order_release);
        SetEvent(signals_.stop.get());
        SetEvent(signals_.command.get());
        if (thread_.joinable())
            thread_.join();
        return false;
    }
    return true;
}

void OverlayExecutor::signal_sources() noexcept {
    if (signals_.source)
        SetEvent(signals_.source.get());
}

void OverlayExecutor::run() noexcept {
    try {
        actor_thread_ = std::this_thread::get_id();
        if (!callbacks_.start || !callbacks_.start()) {
            state_.store(State::failed, std::memory_order_release);
            SetEvent(signals_.ready.get());
            return;
        }
        state_.store(State::running, std::memory_order_release);
        SetEvent(signals_.ready.get());
        run_loop();
        fail_pending(State::stopped, stopped_error());
        if (callbacks_.stop)
            callbacks_.stop();
    } catch (...) {
        const auto failure = std::current_exception();
        fail_pending(State::failed, failure);
        try {
            if (callbacks_.stop)
                callbacks_.stop();
        } catch (...) {
        }
        SetEvent(signals_.ready.get());
    }
}

void OverlayExecutor::run_loop() {
    for (;;) {
        const bool retry = callbacks_.retry_pending && callbacks_.retry_pending();
        const std::array<HANDLE, 3> handles{signals_.stop.get(), signals_.command.get(),
                                            signals_.source.get()};
        const DWORD wait =
            MsgWaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                      retry ? 20 : INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0)
            return;
        if (wait == WAIT_OBJECT_0 + 1)
            process_commands();
        if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_OBJECT_0 + 2 ||
            wait == WAIT_OBJECT_0 + handles.size() || wait == WAIT_TIMEOUT) {
            if (callbacks_.messages)
                callbacks_.messages();
            if (callbacks_.sources)
                callbacks_.sources();
        } else if (wait == WAIT_FAILED) {
            throw std::runtime_error("overlay executor wait failed");
        }
    }
}

void OverlayExecutor::process_commands() noexcept {
    std::array<PendingCommand, 8> batch{};
    size_t count = 0;
    {
        std::lock_guard lock(queue_mutex_);
        ResetEvent(signals_.command.get());
        while (count < batch.size() && !commands_.empty()) {
            batch[count++] = std::move(commands_.front());
            commands_.pop();
        }
        if (!commands_.empty())
            SetEvent(signals_.command.get());
    }
    for (size_t i = 0; i < count; ++i)
        batch[i].run();
}

void OverlayExecutor::fail_pending(State terminal_state, std::exception_ptr failure) noexcept {
    std::queue<PendingCommand> abandoned;
    {
        std::lock_guard lock(queue_mutex_);
        state_.store(terminal_state, std::memory_order_release);
        abandoned.swap(commands_);
    }
    while (!abandoned.empty()) {
        abandoned.front().fail(failure);
        abandoned.pop();
    }
}

} // namespace overwolf_hook::overwolf
