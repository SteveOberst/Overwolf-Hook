#pragma once

#include <windows.h>
#include <cstddef>
#include <memory>

namespace overwolf_hook::ipc {
/** Pagefile-backed Boost.Interprocess storage. The native handle is borrowed;
 * import duplicates a local reference from a verified process. No foreign handle
 * is ever closed. Public C contracts never expose this implementation type. */
class SharedRegion final {
  public:
    static std::unique_ptr<SharedRegion> create(size_t bytes, const char *name = nullptr);
    static std::unique_ptr<SharedRegion> open(const char *name, size_t minimum_bytes);
    static std::unique_ptr<SharedRegion> import(HANDLE process, uint64_t handle, size_t bytes);
    ~SharedRegion();
    SharedRegion(const SharedRegion &) = delete;
    SharedRegion &operator=(const SharedRegion &) = delete;
    void *data() const noexcept;
    size_t size() const noexcept;
    HANDLE handle() const noexcept;

  private:
    SharedRegion();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace overwolf_hook::ipc
