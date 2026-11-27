#pragma once

#include <overwolf_hook/abi/game_state.h>
#include <overwolf_hook/sdk.hpp>

#include <memory>
#include <vector>

namespace overwolf_hook {

namespace detail {

using SnapshotOwner = NativeHandleGuard<owh_state, owh_snapshot_close>;
using SourceListOwner = NativeHandleGuard<owh_channels, owh_source_list_close>;

} // namespace detail

/** A native field record whose provider-byte pointers stay valid while this view lives. */
class StateFieldView final {
  public:
    const owh_state_field &native() const noexcept { return field_; }

  private:
    friend class StateSnapshot;
    StateFieldView(std::shared_ptr<detail::SnapshotOwner> owner, owh_state_field field)
        : owner_(std::move(owner)), field_(field) {}
    std::shared_ptr<detail::SnapshotOwner> owner_;
    owh_state_field field_{};
};

/** Owns one copied native snapshot; field views retain it independently. */
class StateSnapshot final {
  public:
    StateSnapshot(const StateSnapshot &) = delete;
    StateSnapshot &operator=(const StateSnapshot &) = delete;
    StateSnapshot(StateSnapshot &&) noexcept = default;
    StateSnapshot &operator=(StateSnapshot &&) noexcept = default;
    ~StateSnapshot() = default;

    uint32_t count() const {
        auto owner = require_owner();
        return owh_snapshot_count(owner->get());
    }

    StateFieldView at(uint32_t index) const {
        auto owner = require_owner();
        owh_state_field field{};
        check(owh_snapshot_at(owner->get(), index, &field));
        return StateFieldView(std::move(owner), field);
    }

    std::vector<StateFieldView> fields() const {
        auto owner = require_owner();
        const auto size = owh_snapshot_count(owner->get());
        std::vector<StateFieldView> result;
        result.reserve(size);
        for (uint32_t index = 0; index < size; ++index) {
            owh_state_field field{};
            check(owh_snapshot_at(owner->get(), index, &field));
            result.push_back(StateFieldView(owner, field));
        }
        return result;
    }

    void close() noexcept { owner_.reset(); }

  private:
    friend class GameStateStorage;
    explicit StateSnapshot(std::shared_ptr<detail::SnapshotOwner> owner)
        : owner_(std::move(owner)) {}
    std::shared_ptr<detail::SnapshotOwner> require_owner() const {
        auto owner = owner_;
        if (!owner)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return owner;
    }
    std::shared_ptr<detail::SnapshotOwner> owner_;
};

/** Point-in-time list of currently visible source channels. */
class SourceList final {
  public:
    SourceList(const SourceList &) = delete;
    SourceList &operator=(const SourceList &) = delete;
    SourceList(SourceList &&) noexcept = default;
    SourceList &operator=(SourceList &&) noexcept = default;
    ~SourceList() = default;

    uint32_t count() const {
        auto owner = require_owner();
        return owh_source_list_count(owner->get());
    }
    owh_source_channel at(uint32_t index) const {
        auto owner = require_owner();
        owh_source_channel value{};
        check(owh_source_list_at(owner->get(), index, &value));
        return value;
    }
    void close() noexcept { owner_.reset(); }

  private:
    friend class GameStateController;
    explicit SourceList(std::shared_ptr<detail::SourceListOwner> owner)
        : owner_(std::move(owner)) {}
    std::shared_ptr<detail::SourceListOwner> require_owner() const {
        auto owner = owner_;
        if (!owner)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return owner;
    }
    std::shared_ptr<detail::SourceListOwner> owner_;
};

/** Passive descriptor for a source channel; it owns no mapping or snapshot. */
class GameStateStorage final {
  public:
    uint32_t source_channel() const noexcept { return channel_; }
    uint32_t producer_process_id() const noexcept { return producer_; }
    StateSnapshot read(uint32_t timeout_ms = OWH_STATE_DEFAULT_TIMEOUT_MS) const {
        owh_state *state = nullptr;
        const auto status = owh_game_state_read_from(channel_, producer_, timeout_ms, &state);
        detail::NativeHandleGuard<owh_state, owh_snapshot_close> adoption(state);
        check(status);
        if (!state)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto owner = std::make_shared<detail::SnapshotOwner>(adoption.get());
        adoption.release();
        return StateSnapshot(std::move(owner));
    }

  private:
    friend class GameStateController;
    explicit GameStateStorage(uint32_t channel, uint32_t producer) : channel_(channel), producer_(producer) {}
    uint32_t channel_;
    uint32_t producer_;
};

class GameStateController final {
  public:
    GameStateController() = default;
    SourceList sources() const {
        owh_channels *channels = nullptr;
        const auto status = owh_game_state_sources(&channels);
        detail::NativeHandleGuard<owh_channels, owh_source_list_close> adoption(channels);
        check(status);
        if (!channels)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto owner = std::make_shared<detail::SourceListOwner>(adoption.get());
        adoption.release();
        return SourceList(std::move(owner));
    }

    GameStateStorage storage(uint32_t source_channel, uint32_t producer_process_id = 0) const {
        return GameStateStorage(source_channel, producer_process_id);
    }
    GameStateStorage storage(const owh_source_channel &source) const {
        return storage(source.source_channel, source.producer_process_id);
    }

    StateSnapshot read(uint32_t source_channel,
                       uint32_t timeout_ms = OWH_STATE_DEFAULT_TIMEOUT_MS) const {
        return storage(source_channel).read(timeout_ms);
    }
    StateSnapshot read(const owh_source_channel &source,
                       uint32_t timeout_ms = OWH_STATE_DEFAULT_TIMEOUT_MS) const {
        return storage(source).read(timeout_ms);
    }
};

inline GameStateController OverwolfHook::game_state() const { return GameStateController{}; }

} // namespace overwolf_hook
