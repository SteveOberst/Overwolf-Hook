#include <atomic>
#include <cstring>
#include <mutex>
#include <owh/overwolf/overlays/startup_bridge.hpp>
#include <owh/platform/pe_image.hpp>
#include <psapi.h>

namespace overwolf_hook::overwolf {
namespace {
using Lookup = FARPROC(WINAPI *)(HMODULE, LPCSTR);
std::atomic<Lookup> original{};
StartupLookup observer = nullptr;
std::mutex installation;
bool attempted = false, armed = false;

FARPROC WINAPI lookup(HMODULE module, LPCSTR name) {
    const auto result = original.load(std::memory_order_acquire)(module, name);
    const DWORD error = GetLastError();
    if (module && reinterpret_cast<uintptr_t>(name) > 0xffff &&
        std::strcmp(name, "runAgent") == 0 && module == GetModuleHandleW(L"OWAgent.dll"))
        observer(module, result);
    SetLastError(error);
    return result;
}

void **find_import(HMODULE module) {
    MODULEINFO info{};
    if (!K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
        return nullptr;
    const std::span<const uint8_t> bytes(static_cast<const uint8_t *>(info.lpBaseOfDll),
                                         info.SizeOfImage);
    IMAGE_NT_HEADERS64 nt{};
    size_t nt_offset = 0;
    if (!platform::pe::mapped_headers(bytes, nt, nt_offset) ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return nullptr;
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress ||
        !platform::pe::mapped_rva(bytes, nt, directory.VirtualAddress, directory.Size))
        return nullptr;
    void **found = nullptr;
    for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size;
         offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!platform::pe::read(bytes, directory.VirtualAddress + offset, descriptor))
            return nullptr;
        if (!descriptor.Name)
            return found;
        if (!descriptor.OriginalFirstThunk || !descriptor.FirstThunk)
            continue;
        for (size_t i = 0;; ++i) {
            IMAGE_THUNK_DATA64 thunk{};
            const size_t name_slot = size_t{descriptor.OriginalFirstThunk} + i * sizeof(thunk);
            const size_t address_slot = size_t{descriptor.FirstThunk} + i * sizeof(thunk);
            if (!platform::pe::read(bytes, name_slot, thunk) ||
                !platform::pe::fits(address_slot, sizeof(void *), bytes.size()))
                return nullptr;
            if (!thunk.u1.AddressOfData)
                break;
            if (IMAGE_SNAP_BY_ORDINAL64(thunk.u1.Ordinal))
                continue;
            const auto name_rva = thunk.u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name);
            constexpr char wanted[] = "GetProcAddress";
            if (!platform::pe::fits(name_rva, sizeof(wanted), bytes.size()))
                return nullptr;
            if (std::memcmp(bytes.data() + name_rva, wanted, sizeof(wanted)) != 0)
                continue;
            if (found || address_slot % alignof(void *))
                return nullptr;
            found = reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(module) + address_slot);
        }
    }
    return nullptr; // unterminated import descriptor array
}
void **safe_find_import(HMODULE module) noexcept {
    __try {
        return find_import(module);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
} // namespace
bool arm_startup_bridge(HMODULE server, StartupLookup callback) noexcept {
    std::lock_guard lock(installation);
    if (attempted)
        return armed;
    if (!server || !callback || GetModuleHandleW(L"OWAgent.dll"))
        return false;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(server), &pinned))
        return false;
    auto **slot = safe_find_import(server);
    if (!slot)
        return false;
    attempted = true;
    // Do not chain an unidentified patch into the controlled-start guarantee.
    const auto expected = reinterpret_cast<void *>(&GetProcAddress);
    if (*slot != expected)
        return false;
    observer = callback;
    original.store(reinterpret_cast<Lookup>(expected), std::memory_order_release);
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
        return false;
    const bool exchanged = InterlockedCompareExchangePointer(
                               slot, reinterpret_cast<void *>(&lookup), expected) == expected;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(slot, sizeof(*slot), protection, &ignored) != FALSE;
    // The observer, including any partially installed lifecycle trampolines,
    // remains resident even on failure. No physical removal in a live process.
    armed = exchanged && restored && !GetModuleHandleW(L"OWAgent.dll");
    return armed;
}
} // namespace overwolf_hook::overwolf
