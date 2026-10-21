#include <overwolf_hook/discovery.h>
#include "transport/channel.hpp"
#include <winternl.h>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct owh_channels { std::vector<owh_source_channel> entries; };

namespace {
using namespace overwolf_hook;
using OpenDirectory = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
using QueryDirectory = NTSTATUS(NTAPI*)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
using StatusToError = ULONG(NTAPI*)(NTSTATUS);
struct DirectoryEntry { UNICODE_STRING name; UNICODE_STRING type; };
constexpr NTSTATUS no_more_entries = static_cast<NTSTATUS>(0x8000001aL);

// NtQueryDirectoryObject returns pointers into the supplied result buffer.
std::wstring_view bounded(const UNICODE_STRING& text, const std::byte* buffer, size_t size) {
    const auto start = reinterpret_cast<uintptr_t>(buffer);
    const auto address = reinterpret_cast<uintptr_t>(text.Buffer);
    if (text.Length % sizeof(wchar_t) || text.Length > text.MaximumLength ||
        address < start || address - start > size || text.Length > size - (address - start))
        fail(OWH_CORRUPT);
    return {text.Buffer, text.Length / sizeof(wchar_t)};
}
uint32_t channel_id(std::wstring_view digits) {
    if (digits.empty() || digits.size() > 10 || digits.front() == L'0') return 0;
    uint64_t value = 0;
    for (const auto digit : digits) {
        if (digit < L'0' || digit > L'9') return 0;
        value = value * 10 + static_cast<unsigned>(digit - L'0');
        if (value > UINT32_MAX) return 0;
    }
    return static_cast<uint32_t>(value);
}
}

extern "C" OWH_CLIENT owh_result __cdecl owh_channels_discover(uint32_t abi, owh_channels** output) {
    if (output) *output = nullptr;
    return overwolf_hook::boundary([&] {
        if (!output) fail(OWH_INVALID_ARGUMENT);
        if (abi != OWH_ABI_VERSION) fail(OWH_INCOMPATIBLE);
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto open = reinterpret_cast<OpenDirectory>(GetProcAddress(ntdll, "NtOpenDirectoryObject"));
        const auto query = reinterpret_cast<QueryDirectory>(GetProcAddress(ntdll, "NtQueryDirectoryObject"));
        const auto error = reinterpret_cast<StatusToError>(GetProcAddress(ntdll, "RtlNtStatusToDosError"));
        if (!open || !query || !error) fail(OWH_INCOMPATIBLE, ERROR_PROC_NOT_FOUND);
        DWORD session;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) fail(OWH_SYSTEM_ERROR, GetLastError());
        std::wstring path = session ? L"\\Sessions\\" + std::to_wstring(session) + L"\\BaseNamedObjects" : L"\\BaseNamedObjects";
        UNICODE_STRING name{};
        name.Buffer = path.data();
        name.Length = static_cast<USHORT>(path.size() * sizeof(wchar_t));
        name.MaximumLength = name.Length;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes); attributes.ObjectName = &name; attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
        HANDLE raw = nullptr;
        const auto opened = open(&raw, 1, &attributes); // DIRECTORY_QUERY
        if (opened < 0) fail(OWH_SYSTEM_ERROR, error(opened));
        Handle directory(raw);
        std::map<uint32_t, uint32_t> found;
        // Windows UNICODE_STRING lengths are 16-bit. One result (two strings)
        // fits here; buffer exhaustion remains an error rather than lost entries.
        std::vector<std::byte> buffer(132 * 1024);
        ULONG context = 0;
        bool restart = true;
        for (size_t n = 0; ; ++n) {
            if (n == 1000000) fail(OWH_RESOURCE_EXHAUSTED);
            ULONG length = 0;
            const auto status = query(directory.get(), buffer.data(), static_cast<ULONG>(buffer.size()), TRUE,
                restart ? TRUE : FALSE, &context, &length);
            restart = false;
            if (status == no_more_entries) break;
            if (status != 0) fail(OWH_SYSTEM_ERROR, error(status));
            if (length < sizeof(DirectoryEntry) || length > buffer.size()) fail(OWH_CORRUPT);
            const auto& entry = *reinterpret_cast<const DirectoryEntry*>(buffer.data());
            const auto type = bounded(entry.type, buffer.data(), length);
            if (type != L"Section") continue;
            const auto object = bounded(entry.name, buffer.data(), length);
            uint32_t kind = 0, id = 0;
            if (object.starts_with(L"ow_info_")) { kind = OWH_CHANNEL_INFO; id = channel_id(object.substr(8)); }
            else if (object.starts_with(L"ow_events_")) { kind = OWH_CHANNEL_EVENTS; id = channel_id(object.substr(10)); }
            if (id) found[id] |= kind;
        }
        auto channels = std::make_unique<owh_channels>();
        for (const auto& [id, kinds] : found) channels->entries.push_back({id, kinds});
        *output = channels.release();
        return ok;
    });
}
extern "C" OWH_CLIENT uint32_t __cdecl owh_channels_count(const owh_channels* channels) {
    return channels ? static_cast<uint32_t>(channels->entries.size()) : 0;
}
extern "C" OWH_CLIENT owh_result __cdecl owh_channels_at(const owh_channels* channels,
    uint32_t index, owh_source_channel* output) {
    if (output) *output = {};
    if (!channels || !output || index >= channels->entries.size()) return {OWH_INVALID_ARGUMENT, 0};
    *output = channels->entries[index];
    return overwolf_hook::ok;
}
extern "C" OWH_CLIENT void __cdecl owh_channels_close(owh_channels* channels) { delete channels; }
