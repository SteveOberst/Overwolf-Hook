#include <algorithm>
#include <cstring>
#include <owh/overwolf/signatures/event_resolver.hpp>
#include <owh/overwolf/signatures/event_targets.hpp>
#include <owh/platform/result.hpp>
#include <owh/platform/pe_image.hpp>
#include <vector>

namespace overwolf_hook::overwolf::signatures::events {
namespace {
[[noreturn]] void incompatible(DWORD reason) { fail_win32(OWH_INCOMPATIBLE, reason); }
bool fits(size_t offset, size_t length, size_t size) {
    return platform::pe::fits(offset, length, size);
}
template <typename T> T at(std::span<const uint8_t> image, size_t offset) {
    T value{};
    if (!platform::pe::read(image, offset, value))
        incompatible(ERROR_BAD_EXE_FORMAT);
    return value;
}
IMAGE_NT_HEADERS64 headers(std::span<const uint8_t> image, size_t &nt_offset) {
    IMAGE_NT_HEADERS64 nt{};
    if (!platform::pe::mapped_headers(image, nt, nt_offset) ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION)
        incompatible(ERROR_BAD_EXE_FORMAT);
    return nt;
}
template <size_t N>
bool contains(std::span<const uint8_t> bytes, const std::array<int16_t, N> &pattern) {
    if (bytes.size() < N)
        return false;
    return platform::pe::count_pattern(bytes, pattern) != 0;
}
void copy_image_bytes(HMODULE module, size_t offset, void *output, size_t length) {
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<uint8_t *>(module) + offset,
                           output, length, &copied) ||
        copied != length)
        incompatible(ERROR_NOACCESS);
}
} // namespace

size_t event_dispatcher_offset(std::span<const uint8_t> image) {
    size_t nt_offset;
    const auto nt = headers(image, nt_offset);
    size_t sections_offset = 0;
    if (!platform::pe::section_table(image, nt_offset, nt, sections_offset))
        incompatible(ERROR_BAD_EXE_FORMAT);
    const auto table = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!table.Size || table.Size % sizeof(RUNTIME_FUNCTION) ||
        !fits(table.VirtualAddress, table.Size, image.size()))
        incompatible(ERROR_BAD_EXE_FORMAT);
    size_t candidate = 0, candidates = 0;
    for (size_t n = 0; n < table.Size / sizeof(RUNTIME_FUNCTION); ++n) {
        const auto function =
            at<RUNTIME_FUNCTION>(image, table.VirtualAddress + n * sizeof(RUNTIME_FUNCTION));
        if (function.BeginAddress >= function.EndAddress || function.EndAddress > image.size())
            incompatible(ERROR_BAD_EXE_FORMAT);
        bool executable = false;
        for (size_t s = 0; s < nt.FileHeader.NumberOfSections; ++s) {
            const auto section =
                at<IMAGE_SECTION_HEADER>(image, sections_offset + s * sizeof(IMAGE_SECTION_HEADER));
            const size_t end = size_t{section.VirtualAddress} + section.Misc.VirtualSize;
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                function.BeginAddress >= section.VirtualAddress && function.EndAddress <= end &&
                end <= image.size())
                executable = true;
        }
        if (!executable)
            continue;
        const auto body =
            image.subspan(function.BeginAddress, function.EndAddress - function.BeginAddress);
        if (contains(body, dispatcher_records) && contains(body, dispatcher_callback)) {
            candidate = function.BeginAddress;
            ++candidates;
        }
    }
    if (!candidates)
        incompatible(ERROR_PROC_NOT_FOUND);
    if (candidates != 1)
        incompatible(ERROR_DUP_NAME);
    if (!fits(candidate, dispatcher_entry.size(), image.size()) ||
        std::memcmp(image.data() + candidate, dispatcher_entry.data(), dispatcher_entry.size()) != 0)
        incompatible(ERROR_INVALID_DATA);
    return candidate;
}

void *resolve_event_dispatcher(HMODULE module) {
    // Copy just headers, unwind records and executable sections. Reading an
    // entire loaded image would also touch legitimate no-access section gaps.
    std::vector<uint8_t> image(4096);
    copy_image_bytes(module, 0, image.data(), image.size());
    const auto dos = at<IMAGE_DOS_HEADER>(image, 0);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        incompatible(ERROR_BAD_EXE_FORMAT);
    const auto nt = at<IMAGE_NT_HEADERS64>(image, static_cast<size_t>(dos.e_lfanew));
    const size_t size = nt.OptionalHeader.SizeOfImage;
    if (size < 4096 || size > 256 * 1024 * 1024 || nt.OptionalHeader.SizeOfHeaders > size)
        incompatible(ERROR_BAD_EXE_FORMAT);
    image.resize(size);
    size_t nt_offset;
    headers(image, nt_offset);
    copy_image_bytes(module, 0, image.data(), nt.OptionalHeader.SizeOfHeaders);
    size_t sections_offset = 0;
    if (!platform::pe::section_table(image, nt_offset, nt, sections_offset))
        incompatible(ERROR_BAD_EXE_FORMAT);
    for (size_t s = 0; s < nt.FileHeader.NumberOfSections; ++s) {
        const auto section =
            at<IMAGE_SECTION_HEADER>(image, sections_offset + s * sizeof(IMAGE_SECTION_HEADER));
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;
        if (!fits(section.VirtualAddress, section.Misc.VirtualSize, size))
            incompatible(ERROR_BAD_EXE_FORMAT);
        copy_image_bytes(module, section.VirtualAddress, image.data() + section.VirtualAddress,
                         section.Misc.VirtualSize);
    }
    const auto table = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!fits(table.VirtualAddress, table.Size, size))
        incompatible(ERROR_BAD_EXE_FORMAT);
    copy_image_bytes(module, table.VirtualAddress, image.data() + table.VirtualAddress, table.Size);
    return reinterpret_cast<uint8_t *>(module) + event_dispatcher_offset(image);
}
} // namespace overwolf_hook::overwolf::signatures::events
