#pragma once
#include "types.hpp"
#include <overwolf_hook/types/build_info.h>
#include <type_traits>
namespace overwolf_hook::core {
struct HelloRequest {
    uint32_t abi_version, client_process_id, windows_session_id, reserved;
    uint64_t client_process_started, host_process_started;
    uint32_t wire_protocol;
    owh_build_info build_info;
    SessionPolicy options{};
};
struct HelloResponse {
    owh_result result;
    uint32_t abi_version, wire_protocol, host_process_id, reserved;
    uint64_t host_process_started, session_id;
    owh_build_info build_info;
    SessionPolicy policy;
};
struct Empty {};
struct ResultResponse {
    owh_result result;
};
struct OverlayCapabilitiesResponse {
    owh_result result;
    owh_overlay_capabilities value;
};
struct EventCapabilitiesResponse {
    owh_result result;
    owh_event_capabilities value;
};
struct CreatePixelBufferSourceRequest {
    ResourceRef ref;
    Geometry geometry;
    PixelBufferRef queue;
};
struct CreateSharedTextureSourceRequest {
    ResourceRef ref;
    Geometry geometry;
};
struct CreateSourceResponse {
    owh_result result;
    ResourceRef ref;
};
struct SourceRequest {
    ResourceRef ref;
};
struct BindD3D11Request {
    ResourceRef ref;
    LegacySharedTexture texture;
};
struct OverlayInfoResponse {
    owh_result result;
    owh_overlay_info_data info;
};
struct SubscribeEventsRequest {
    ResourceRef ref;
    uint32_t source_channel, reserved;
};
struct SubscribeEventsResponse {
    owh_result result;
    ResourceRef ref;
    char channel[104];
    uint64_t start_sequence, capture_loss, wake_handle, closed_handle;
};
struct EventStatisticsResponse {
    owh_result result;
    owh_subscription_info info;
};
template <uint16_t Kind, class Req, class Resp> struct Command {
    using Request = Req;
    using Response = Resp;
    static constexpr uint16_t kind = Kind;
};
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    using Symbol = Command<Id, RequestType, ResponseType>;
#include "events.def"
#include "overlays.def"
#include "session.def"
#undef OWH_METHOD
template <class T> constexpr uint32_t wire_size = std::is_same_v<T, Empty> ? 0 : sizeof(T);
static_assert(std::is_trivially_copyable_v<CreatePixelBufferSourceRequest>);
} // namespace overwolf_hook::core
