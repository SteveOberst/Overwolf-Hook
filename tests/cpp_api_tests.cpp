#include <overwolf_hook/overwolf_hook.hpp>

#include <cassert>
#include <span>
#include <type_traits>
#include <utility>

using namespace overwolf_hook;

static_assert(!std::is_copy_constructible_v<OverwolfHook>);
static_assert(!std::is_copy_constructible_v<PixelFrame>);
static_assert(!std::is_copy_constructible_v<PixelBufferOverlay>);
static_assert(!std::is_copy_constructible_v<SharedTextureOverlay>);
static_assert(!std::is_copy_constructible_v<EventSubscription>);
static_assert(std::is_move_constructible_v<WaitCancellation>);
static_assert(
    std::is_same_v<decltype(std::declval<PixelFrame &>().pixels()), std::span<std::byte>>);
static_assert(std::is_same_v<decltype(std::declval<const EventSubscription &>().receive()),
                             std::optional<owh_event>>);
static_assert(sizeof(owh_subscription_info) == 64);
static_assert(OWH_ABI_VERSION == 1);

void close_consumes_local_handle() {
    int native_value = 7;
    detail::HandleSlot<int> slot(&native_value);
    assert(slot.get() == &native_value);
    const auto result = slot.close_result([&](int *value) {
        assert(value == &native_value);
        return owh_result{OWH_OK, 0, 0, 0, 0};
    });
    assert(result.status == OWH_OK);
    try {
        (void)slot.get();
        assert(false && "consumed handle accepted a new call");
    } catch (const NativeError &error) {
        assert(error.result().status == OWH_CLOSED);
    }
}

int main() {
    owh_client_options options{};
    owh_client_options_init(&options);
    assert(options.size == sizeof(options));
    assert(options.abi_version == OWH_ABI_VERSION);

    assert(OverwolfHook::abi_version() == OWH_ABI_VERSION);
    GameStateController passive_state;
    (void)passive_state;
    auto sdk = OverwolfHook::open();
    auto overlays = sdk.overlays();
    auto events = sdk.events();
    auto state = sdk.game_state();
    (void)overlays;
    (void)events;
    (void)state;
    sdk.close();
    (void)sdk.game_state(); // The passive convenience accessor has no SDK lifecycle dependency.
    try {
        (void)sdk.agent_info();
        assert(false && "closed SDK accepted a call");
    } catch (const NativeError &error) {
        assert(error.result().status == OWH_CLOSED);
    }

    close_consumes_local_handle();
}
