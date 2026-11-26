#pragma once
#include "session/attached_session.hpp"

namespace overwolf_hook::client {
/** Independent application context. Controllers capture a session once per
 * operation; resources retain that session, never a pointer back to the SDK. */
class Sdk final {
  public:
    ~Sdk() { (void)detach(); }
    owh_result attach(const owh_client_options *options) noexcept;
    owh_result detach() noexcept;
    std::pair<owh_result, std::shared_ptr<AttachedSession>> session() const noexcept;

  private:
    friend struct test_fixture::Access;
    std::shared_ptr<AttachedSession> connection_;
};
} // namespace overwolf_hook::client
