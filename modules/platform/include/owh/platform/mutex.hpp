#pragma once
#include <owh/platform/result.hpp>

namespace overwolf_hook {
class Lock {
  public:
    explicit Lock(HANDLE mutex, DWORD timeout);
    Lock(HANDLE mutex, DWORD timeout, HANDLE cancel);
    ~Lock() {
        if (owned_)
            ReleaseMutex(mutex_);
    }
    bool abandoned() const { return abandoned_; }

  private:
    HANDLE mutex_;
    bool owned_ = false;
    bool abandoned_ = false;
};

} // namespace overwolf_hook
