#pragma once
#include <overwolf_hook/abi/sdk.h>
#include <stdexcept>
namespace owh_test {
class Sdk {
  public:
    Sdk() {
        if (owh_sdk_open(&value).status != OWH_OK)
            throw std::runtime_error("SDK context creation failed");
    }
    ~Sdk() { owh_sdk_close(value); }
    Sdk(const Sdk &) = delete;
    Sdk &operator=(const Sdk &) = delete;
    owh_sdk *value = nullptr;
};
inline owh_sdk *passive_sdk() {
    static Sdk sdk;
    return sdk.value;
}
} // namespace owh_test
