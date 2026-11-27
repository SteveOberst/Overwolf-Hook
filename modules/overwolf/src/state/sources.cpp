#include <windows.h>
#include <map>
#include <algorithm>
#include <owh/overwolf/state_storage.hpp>
#include <owh/overwolf/provider_sources.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/result.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <winternl.h>

namespace overwolf_hook::overwolf {
namespace {
using OpenDirectory = NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
using QueryDirectory = NTSTATUS(NTAPI *)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
using StatusToError = ULONG(NTAPI *)(NTSTATUS);
struct DirectoryEntry {
    UNICODE_STRING name;
    UNICODE_STRING type;
};
constexpr NTSTATUS no_more_entries = static_cast<NTSTATUS>(0x8000001aL);

std::wstring_view bounded(const UNICODE_STRING &text, const std::byte *buffer, size_t size) {
    const auto start = reinterpret_cast<uintptr_t>(buffer);
    const auto address = reinterpret_cast<uintptr_t>(text.Buffer);
    if (text.Length % sizeof(wchar_t) || text.Length > text.MaximumLength || address < start ||
        address - start > size || text.Length > size - (address - start))
        fail(OWH_CORRUPT, OWH_STAGE_TARGET_DISCOVERY);
    return {text.Buffer, text.Length / sizeof(wchar_t)};
}
uint32_t channel_id(std::wstring_view digits) {
    if (digits.empty() || digits.size() > 10 || digits.front() == L'0')
        return 0;
    uint64_t value = 0;
    for (const auto digit : digits) {
        if (digit < L'0' || digit > L'9')
            return 0;
        value = value * 10 + static_cast<unsigned>(digit - L'0');
        if (value > UINT32_MAX)
            return 0;
    }
    return static_cast<uint32_t>(value);
}
} // namespace

owh_result discover_sources(std::vector<owh_source_channel> &output) noexcept {
    output.clear();
    return boundary([&] {
        const auto ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto open =
            reinterpret_cast<OpenDirectory>(GetProcAddress(ntdll, "NtOpenDirectoryObject"));
        const auto query =
            reinterpret_cast<QueryDirectory>(GetProcAddress(ntdll, "NtQueryDirectoryObject"));
        const auto error =
            reinterpret_cast<StatusToError>(GetProcAddress(ntdll, "RtlNtStatusToDosError"));
        if (!open || !query || !error)
            fail_win32(OWH_INCOMPATIBLE, ERROR_PROC_NOT_FOUND, OWH_STAGE_TARGET_DISCOVERY);
        DWORD session = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
            fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_TARGET_DISCOVERY);
        std::wstring path = session
                                ? L"\\Sessions\\" + std::to_wstring(session) + L"\\BaseNamedObjects"
                                : L"\\BaseNamedObjects";
        UNICODE_STRING name{};
        name.Buffer = path.data();
        name.Length = static_cast<USHORT>(path.size() * sizeof(wchar_t));
        name.MaximumLength = name.Length;
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        attributes.ObjectName = &name;
        attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
        HANDLE raw = nullptr;
        const auto opened = open(&raw, 1, &attributes); // DIRECTORY_QUERY
        if (opened < 0)
            fail_win32(OWH_SYSTEM_ERROR, error(opened), OWH_STAGE_TARGET_DISCOVERY);
        Handle directory(raw);
        std::map<uint32_t, uint32_t> found;
        std::vector<std::byte> buffer(132 * 1024);
        ULONG context = 0;
        bool restart = true;
        for (size_t n = 0;; ++n) {
            if (n == 1000000)
                fail(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_TARGET_DISCOVERY);
            ULONG length = 0;
            const auto status =
                query(directory.get(), buffer.data(), static_cast<ULONG>(buffer.size()), TRUE,
                      restart ? TRUE : FALSE, &context, &length);
            restart = false;
            if (status == no_more_entries)
                break;
            if (status != 0)
                fail_win32(OWH_SYSTEM_ERROR, error(status), OWH_STAGE_TARGET_DISCOVERY);
            if (length < sizeof(DirectoryEntry) || length > buffer.size())
                fail(OWH_CORRUPT, OWH_STAGE_TARGET_DISCOVERY);
            const auto &entry = *reinterpret_cast<const DirectoryEntry *>(buffer.data());
            if (bounded(entry.type, buffer.data(), length) != L"Section")
                continue;
            const auto object = bounded(entry.name, buffer.data(), length);
            uint32_t kind = 0, id = 0;
            if (object.starts_with(L"ow_info_")) {
                kind = OWH_CHANNEL_INFO;
                id = channel_id(object.substr(8));
            } else if (object.starts_with(L"ow_events_")) {
                kind = OWH_CHANNEL_EVENTS;
                id = channel_id(object.substr(10));
            }
            if (id)
                found[id] |= kind;
        }
        output.reserve(found.size());
        for (const auto &[id, kinds] : found)
            output.push_back({id, kinds});
        std::vector<ProviderSource> providers;
        const auto status = provider_sources(providers);
        // Classic sources remain useful independently of a newer consumer's
        // registry. An unsupported registry must not hide them.
        if (status.status != OWH_OK)
            return output.empty() ? status : ok;
        for (const auto &source : providers) {
            uint32_t kinds = 0;
            if (!source.info_mapping.empty()) kinds |= OWH_CHANNEL_INFO;
            if (!source.events_mapping.empty()) kinds |= OWH_CHANNEL_EVENTS;
            // A registry association enriches the same classic mapping; it is
            // not an additional anonymous source. Keep any unassociated kinds.
            for (auto &classic : output) {
                if (classic.source_channel != source.source_channel || classic.producer_process_id)
                    continue;
                if (source.info_mapping == L"ow_info_" + std::to_wstring(source.source_channel))
                    classic.kinds &= ~OWH_CHANNEL_INFO;
                if (source.events_mapping == L"ow_events_" + std::to_wstring(source.source_channel))
                    classic.kinds &= ~OWH_CHANNEL_EVENTS;
            }
            if (kinds) output.push_back({source.source_channel, kinds, source.producer_process_id});
        }
        std::erase_if(output, [](const auto &source) { return !source.kinds; });
        std::sort(output.begin(), output.end(), [](const auto &a, const auto &b) {
            return std::pair{a.source_channel, a.producer_process_id} <
                   std::pair{b.source_channel, b.producer_process_id};
        });
        return ok;
    });
}
} // namespace overwolf_hook::overwolf
