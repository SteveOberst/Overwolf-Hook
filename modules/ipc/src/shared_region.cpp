#include <windows.h>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/permissions.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>
#include <owh/ipc/shared_region.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/result.hpp>
#include <sddl.h>
#include <vector>

namespace overwolf_hook::ipc {
namespace bip = boost::interprocess;
namespace {
struct BorrowedMapping {
    HANDLE value;
    bip::mapping_handle_t get_mapping_handle() const noexcept { return {value, true}; }
};
class UserPermissions {
    HLOCAL descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};

  public:
    UserPermissions() {
        Handle token;
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
            fail_win32(OWH_SYSTEM_ERROR, GetLastError());
        token.reset(raw);
        DWORD count = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &count);
        std::vector<std::byte> storage(count);
        if (!count || !GetTokenInformation(token.get(), TokenUser, storage.data(), count, &count))
            fail_win32(OWH_SYSTEM_ERROR, GetLastError());
        LPSTR sid = nullptr;
        if (!ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid))
            fail_win32(OWH_SYSTEM_ERROR, GetLastError());
        std::string sddl = "D:P(A;;GA;;;" + std::string(sid) + ")(A;;GA;;;SY)";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1,
                                                                  &descriptor, nullptr))
            fail_win32(OWH_SYSTEM_ERROR, GetLastError());
        descriptor_ = descriptor;
        attributes_.lpSecurityDescriptor = descriptor;
    }
    ~UserPermissions() {
        if (descriptor_)
            LocalFree(descriptor_);
    }
    bip::permissions get() { return bip::permissions(&attributes_); }
};
} // namespace
struct SharedRegion::Impl {
    bip::windows_shared_memory memory;
    Handle imported;
    bip::mapped_region view;
    HANDLE handle() const noexcept {
        return imported ? imported.get() : memory.get_mapping_handle().handle;
    }
};
SharedRegion::SharedRegion() : impl_(std::make_unique<Impl>()) {}
SharedRegion::~SharedRegion() = default;
std::unique_ptr<SharedRegion> SharedRegion::create(size_t bytes, const char *name) {
    if (!bytes)
        fail(OWH_INVALID_ARGUMENT);
    auto result = std::unique_ptr<SharedRegion>(new SharedRegion());
    UserPermissions permissions;
    try {
        result->impl_->memory = bip::windows_shared_memory(bip::create_only, name, bip::read_write,
                                                           bytes, permissions.get());
        result->impl_->view = bip::mapped_region(result->impl_->memory, bip::read_write, 0, bytes);
    } catch (const bip::interprocess_exception &error) {
        fail_win32(error.get_error_code() == bip::already_exists_error ? OWH_ALREADY_EXISTS
                                                                       : OWH_SYSTEM_ERROR,
                   static_cast<DWORD>(error.get_native_error()));
    }
    return result;
}
std::unique_ptr<SharedRegion> SharedRegion::open(const char *name, size_t minimum_bytes) {
    if (!name)
        fail(OWH_INVALID_ARGUMENT);
    auto result = std::unique_ptr<SharedRegion>(new SharedRegion());
    try {
        result->impl_->memory = bip::windows_shared_memory(bip::open_only, name, bip::read_write);
        if (result->impl_->memory.get_size() < static_cast<bip::offset_t>(minimum_bytes))
            fail(OWH_CORRUPT);
        result->impl_->view =
            bip::mapped_region(result->impl_->memory, bip::read_write, 0, minimum_bytes);
    } catch (const bip::interprocess_exception &error) {
        fail_win32(error.get_error_code() == bip::not_found_error ? OWH_NOT_FOUND
                                                                  : OWH_SYSTEM_ERROR,
                   static_cast<DWORD>(error.get_native_error()));
    }
    return result;
}
std::unique_ptr<SharedRegion> SharedRegion::import(HANDLE process, uint64_t handle, size_t bytes) {
    if (!process || !handle || !bytes)
        fail(OWH_INVALID_ARGUMENT);
    auto result = std::unique_ptr<SharedRegion>(new SharedRegion());
    HANDLE local = nullptr;
    if (!DuplicateHandle(process, reinterpret_cast<HANDLE>(handle), GetCurrentProcess(), &local,
                         FILE_MAP_READ | FILE_MAP_WRITE, FALSE, 0))
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    result->impl_->imported.reset(local);
    try {
        result->impl_->view = bip::mapped_region(BorrowedMapping{local}, bip::read_write, 0, bytes);
    } catch (const bip::interprocess_exception &error) {
        fail_win32(OWH_SYSTEM_ERROR, static_cast<DWORD>(error.get_native_error()));
    }
    return result;
}
void *SharedRegion::data() const noexcept { return impl_->view.get_address(); }
size_t SharedRegion::size() const noexcept { return impl_->view.get_size(); }
HANDLE SharedRegion::handle() const noexcept { return impl_->handle(); }
} // namespace overwolf_hook::ipc
