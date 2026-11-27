#include <owh/overwolf/provider_sources.hpp>

#include <owh/overwolf/host_process.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/pe_image.hpp>
#include <owh/platform/result.hpp>
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace overwolf_hook::overwolf {
namespace {
namespace pe = platform::pe;
constexpr size_t maximum_sources = 4096;
constexpr size_t maximum_image_bytes = 256 * 1024 * 1024;
constexpr uint32_t discovery_stage = OWH_STAGE_TARGET_DISCOVERY;

[[noreturn]] void unsupported(DWORD error = ERROR_INVALID_DATA) {
    fail_win32(OWH_INCOMPATIBLE, error, OWH_STAGE_PROFILE_RESOLVE);
}
[[noreturn]] void corrupt() { fail(OWH_CORRUPT, discovery_stage); }

template <class T> T at(std::span<const uint8_t> bytes, size_t offset) {
    T value{};
    if (!pe::read(bytes, offset, value))
        unsupported(ERROR_BAD_EXE_FORMAT);
    return value;
}

struct Image {
    IMAGE_NT_HEADERS64 nt;
    size_t sections;
};
Image describe(std::span<const uint8_t> bytes) {
    Image image{};
    size_t nt_offset = 0;
    if (!pe::mapped_headers(bytes, image.nt, nt_offset) ||
        image.nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION ||
        !pe::section_table(bytes, nt_offset, image.nt, image.sections))
        unsupported(ERROR_BAD_EXE_FORMAT);
    return image;
}
bool section_contains(std::span<const uint8_t> bytes, const Image &image, size_t address,
                      size_t length, DWORD required, DWORD forbidden = 0) {
    for (size_t n = 0; n < image.nt.FileHeader.NumberOfSections; ++n) {
        const auto section = at<IMAGE_SECTION_HEADER>(bytes, image.sections + n * 40);
        if ((section.Characteristics & required) != required ||
            (section.Characteristics & forbidden) || address < section.VirtualAddress)
            continue;
        if (pe::fits(address - section.VirtualAddress, length, section.Misc.VirtualSize) &&
            pe::fits(address, length, bytes.size()))
            return true;
    }
    return false;
}

uint64_t added(uint64_t address, size_t offset) {
    if (address < 65536 || offset > UINT64_MAX - address)
        corrupt();
    return address + offset;
}
void copy(HANDLE process, uint64_t address, void *output, size_t length) {
    if (!length)
        return;
    added(address, length);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void *>(address), output, length,
                           &copied) || copied != length)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), discovery_stage);
}
template <class T> T remote(HANDLE process, uint64_t address) {
    T value{};
    copy(process, address, &value, sizeof(value));
    return value;
}
struct NativeString {
    uint64_t storage[2];
    uint64_t length;
    uint64_t capacity;
};
static_assert(sizeof(NativeString) == 32);

std::string string(HANDLE process, uint64_t address, size_t limit) {
    const auto source = remote<NativeString>(process, address);
    if (source.length > limit || source.capacity < source.length || source.capacity < 15)
        corrupt();
    const uint64_t data = source.capacity > 15 ? source.storage[0] : address;
    std::string result(static_cast<size_t>(source.length) + 1, '\0');
    copy(process, data, result.data(), result.size());
    if (result.back() != '\0')
        corrupt();
    result.pop_back();
    return result;
}
std::wstring mapping_name(HANDLE process, uint64_t service) {
    auto text = string(process, added(service, 8), 63);
    // Returned connection names are bounded ASCII object names, not paths.
    // Preserve their exact spelling rather than inferring a channel from them.
    for (const unsigned char ch : text)
        if (ch < 0x20 || ch > 0x7e || ch == '\\' || ch == '/')
            corrupt();
    if (text.empty())
        corrupt();
    return {text.begin(), text.end()};
}

struct Module {
    uint64_t base = 0;
    size_t size = 0;
};
Module consumer_module(uint32_t pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), discovery_stage);
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    Module found;
    if (!Module32FirstW(snapshot.get(), &entry)) {
        const auto error = GetLastError();
        if (error != ERROR_NO_MORE_FILES)
            fail_win32(OWH_SYSTEM_ERROR, error, discovery_stage);
        return found;
    }
    do {
        if (_wcsicmp(entry.szModule, L"OWGameEventsConsumer.dll") != 0)
            continue;
        if (found.base)
            unsupported(ERROR_DUP_NAME);
        found = {reinterpret_cast<uint64_t>(entry.modBaseAddr), entry.modBaseSize};
    } while (Module32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), discovery_stage);
    return found;
}

std::vector<uint8_t> image_copy(HANDLE process, const Module &module) {
    if (module.size < 4096 || module.size > maximum_image_bytes)
        unsupported(ERROR_BAD_EXE_FORMAT);
    std::vector<uint8_t> image(module.size);
    copy(process, module.base, image.data(), 4096);
    const auto headers = describe(image);
    if (headers.nt.OptionalHeader.SizeOfImage != module.size ||
        headers.nt.OptionalHeader.SizeOfHeaders > module.size)
        unsupported(ERROR_BAD_EXE_FORMAT);
    copy(process, module.base, image.data(), headers.nt.OptionalHeader.SizeOfHeaders);
    const auto complete = describe(image);
    // Code, exports and unwind metadata are immutable readable sections. Never
    // read whole mapped gaps or assume the remote image is one readable region.
    for (size_t n = 0; n < complete.nt.FileHeader.NumberOfSections; ++n) {
        const auto section = at<IMAGE_SECTION_HEADER>(image, complete.sections + n * 40);
        if (!(section.Characteristics & IMAGE_SCN_MEM_READ) ||
            (section.Characteristics & IMAGE_SCN_MEM_WRITE))
            continue;
        if (!pe::fits(section.VirtualAddress, section.Misc.VirtualSize, image.size()))
            unsupported(ERROR_BAD_EXE_FORMAT);
        copy(process, added(module.base, section.VirtualAddress),
             image.data() + section.VirtualAddress, section.Misc.VirtualSize);
    }
    return image;
}

struct Registry {
    uint64_t head;
    uint64_t count;
    bool operator==(const Registry &) const = default;
};
struct Node {
    uint64_t previous;
    uint64_t next;
    uint32_t channel;
    uint32_t producer;
    uint64_t controller;
    bool operator==(const Node &) const = default;
};
static_assert(sizeof(Node) == 32);

bool snapshot(HANDLE process, uint64_t registry, std::vector<ProviderSource> &output) {
    const auto before = remote<Registry>(process, registry);
    if (before.count > maximum_sources || !before.head)
        corrupt();
    const auto sentinel = remote<Node>(process, before.head);
    auto node_address = sentinel.next;
    uint64_t previous = before.head;
    std::set<uint64_t> visited;
    std::set<std::pair<uint32_t, uint32_t>> identities;
    while (node_address != before.head) {
        if (visited.size() >= maximum_sources || !visited.insert(node_address).second)
            corrupt();
        const auto node = remote<Node>(process, node_address);
        if (node.previous != previous || !node.channel || !node.producer ||
            !identities.emplace(node.channel, node.producer).second)
            return false;
        // Newly inserted controllers may briefly be null before Initialize sets
        // their delegate/readers. They are not yet usable source records.
        if (node.controller) {
            const auto delegate = remote<uint64_t>(process, added(node.controller, 8));
            if (delegate) {
                const auto identity = remote<uint64_t>(process, added(delegate, 8));
                if (static_cast<uint32_t>(identity) != node.channel ||
                    static_cast<uint32_t>(identity >> 32) != node.producer)
                    return false;
                ProviderSource source;
                source.source_channel = node.channel;
                source.producer_process_id = node.producer;
                const auto events = remote<uint64_t>(process, added(node.controller, 0x40));
                const auto info = remote<uint64_t>(process, added(node.controller, 0x78));
                if (events)
                    source.events_mapping = mapping_name(
                        process, remote<uint64_t>(process, added(events, 0x38)));
                if (info)
                    source.info_mapping = mapping_name(
                        process, remote<uint64_t>(process, added(info, 0x50)));
                source.value_key = string(process, added(node.controller, 0xf0), 15);
                if (events || info)
                    output.push_back(std::move(source));
            }
        }
        if (!(remote<Node>(process, node_address) == node))
            return false;
        previous = node_address;
        node_address = node.next;
    }
    const auto after_sentinel = remote<Node>(process, before.head);
    return visited.size() == before.count && previous == sentinel.previous &&
           after_sentinel.previous == sentinel.previous && after_sentinel.next == sentinel.next &&
           remote<Registry>(process, registry) == before;
}
} // namespace

std::vector<std::byte> decode_provider_value(std::span<const std::byte> bytes, std::string_view key) {
    if (bytes.empty() || key.empty()) return {bytes.begin(), bytes.end()};
    // Consumer's Base64 decoder skips nonalphabet bytes and stops at '='.
    // Keep those observed semantics, including unchanged input below 9 bytes.
    std::vector<std::byte> decoded;
    decoded.reserve(bytes.size() * 3 / 4);
    uint32_t bits = 0;
    unsigned count = 0;
    for (const auto byte : bytes) {
        const auto ch = std::to_integer<unsigned char>(byte);
        if (ch == '=') break;
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') value = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') value = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') value = ch - '0' + 52;
        else if (ch == '+') value = 62;
        else if (ch == '/') value = 63;
        if (value < 0) continue;
        bits = (bits << 6) | static_cast<uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            decoded.push_back(static_cast<std::byte>((bits >> count) & 255));
        }
    }
    if (decoded.size() < 9) return {bytes.begin(), bytes.end()};
    std::vector<std::byte> output(decoded.size() - 8);
    for (size_t i = 0; i < output.size(); ++i)
        output[i] = decoded[i + 8] ^ decoded[i & 7] ^
                    static_cast<std::byte>(static_cast<unsigned char>(key[i % key.size()]));
    return output;
}

size_t provider_registry_head_offset(std::span<const uint8_t> image) {
    const auto headers = describe(image);
    DWORD getter = 0;
    if (!pe::export_rva(image, pe::Layout::mapped_image, headers.nt, headers.sections,
                        "GetDBinfo", getter))
        unsupported(ERROR_PROC_NOT_FOUND);
    const auto table = headers.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!table.Size || table.Size % sizeof(RUNTIME_FUNCTION) ||
        !pe::fits(table.VirtualAddress, table.Size, image.size()))
        unsupported(ERROR_BAD_EXE_FORMAT);
    std::span<const uint8_t> body;
    for (size_t n = 0; n < table.Size; n += sizeof(RUNTIME_FUNCTION)) {
        const auto function = at<RUNTIME_FUNCTION>(image, table.VirtualAddress + n);
        if (function.BeginAddress != getter)
            continue;
        if (!body.empty() || function.BeginAddress >= function.EndAddress ||
            function.EndAddress - function.BeginAddress > 65536 ||
            !section_contains(image, headers, getter, function.EndAddress - getter,
                              IMAGE_SCN_MEM_EXECUTE))
            unsupported(ERROR_BAD_EXE_FORMAT);
        body = image.subspan(getter, function.EndAddress - getter);
    }
    // These independent ABI anchors establish the observed registry node's
    // controller slot and the 112-byte InfoDB snapshot record loop.
    constexpr std::array<int16_t, 4> controller{0x48, 0x8b, 0x7a, 0x18};
    constexpr std::array<int16_t, 14> records{0x48, 0x6b, 0xcb, 0x70, 0x49, 0x03, 0xc8,
                                            0x4c, 0x8d, 0x41, 0x20, 0x49, 0x83, 0x78};
    constexpr std::array<int16_t, 7> head_compare{0x48, 0x3b, 0x15, -1, -1, -1, -1};
    if (body.empty() || pe::count_pattern(body, controller) != 1 ||
        pe::count_pattern(body, records) != 1)
        unsupported(ERROR_PROC_NOT_FOUND);
    size_t instruction = 0;
    const auto count = pe::count_pattern(body, head_compare, &instruction);
    if (count != 1)
        unsupported(count ? ERROR_DUP_NAME : ERROR_PROC_NOT_FOUND);
    const auto displacement = at<int32_t>(body, instruction + 3);
    const int64_t target = static_cast<int64_t>(getter) + static_cast<int64_t>(instruction) + 7 +
                           displacement;
    if (target < 0 || target % 8 ||
        !section_contains(image, headers, static_cast<size_t>(target), sizeof(Registry),
                          IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, IMAGE_SCN_MEM_EXECUTE))
        unsupported(ERROR_INVALID_ADDRESS);
    return static_cast<size_t>(target);
}

owh_result provider_sources(std::vector<ProviderSource> &output) noexcept {
    output.clear();
    return boundary([&] {
        auto [result, host] = select_process(0, 0, ProcessAccess::read_memory);
        if (result.status == OWH_NOT_FOUND)
            return ok;
        if (result.status != OWH_OK)
            return result;
        const auto module = consumer_module(host.pid);
        if (!module.base)
            return ok;
        const auto image = image_copy(host.process.get(), module);
        const auto slot = provider_registry_head_offset(image);
        const auto registry = added(module.base, slot);
        for (size_t attempt = 0; attempt < 3; ++attempt) {
            std::vector<ProviderSource> candidate;
            if (snapshot(host.process.get(), registry, candidate)) {
                output = std::move(candidate);
                return ok;
            }
        }
        return make_result(OWH_BUSY, discovery_stage);
    });
}
} // namespace overwolf_hook::overwolf
