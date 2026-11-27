#pragma once
#include "service_contracts.hpp"

namespace overwolf_hook::core {
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    virtual ResponseType Name(const RequestType &request) noexcept = 0;
class SessionService {
  public:
    virtual ~SessionService() = default;
#include "session.def"
};
class OverlayService {
  public:
    virtual ~OverlayService() = default;
#include "overlays.def"
};
class EventService {
  public:
    virtual ~EventService() = default;
#include "events.def"
};
#undef OWH_METHOD
} // namespace overwolf_hook::core
