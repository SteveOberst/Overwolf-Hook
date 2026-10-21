#include "reader.hpp"
#include "profile.hpp"
#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace overwolf_hook::overwolf {
namespace {
[[noreturn]] void incompatible(DWORD reason) { fail(OWH_INCOMPATIBLE, reason); }
bool fits(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
template<typename T> T at(std::span<const uint8_t> image, size_t offset) {
    if (!fits(offset, sizeof(T), image.size())) incompatible(ERROR_BAD_EXE_FORMAT);
    T value;
    std::memcpy(&value, image.data() + offset, sizeof(value));
    return value;
}
IMAGE_NT_HEADERS64 headers(std::span<const uint8_t> image, size_t& nt_offset) {
    const auto dos = at<IMAGE_DOS_HEADER>(image, 0);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) incompatible(ERROR_BAD_EXE_FORMAT);
    nt_offset = static_cast<size_t>(dos.e_lfanew);
    const auto nt = at<IMAGE_NT_HEADERS64>(image, nt_offset);
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION ||
        nt.OptionalHeader.SizeOfImage > image.size()) incompatible(ERROR_BAD_EXE_FORMAT);
    return nt;
}
template<size_t N> bool contains(std::span<const uint8_t> bytes, const std::array<int16_t, N>& pattern) {
    if (bytes.size() < N) return false;
    for (size_t start = 0; start <= bytes.size() - N; ++start) {
        size_t i = 0;
        while (i < N && (pattern[i] < 0 || pattern[i] == bytes[start + i])) ++i;
        if (i == N) return true;
    }
    return false;
}
void copy_image_bytes(HMODULE module, size_t offset, void* output, size_t length) {
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<uint8_t*>(module) + offset,
        output, length, &copied) || copied != length) incompatible(ERROR_NOACCESS);
}
}

size_t reader_offset(std::span<const uint8_t> image) {
    size_t nt_offset;
    const auto nt = headers(image, nt_offset);
    const auto sections_offset = nt_offset + sizeof(IMAGE_NT_HEADERS64);
    if (!nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96 ||
        !fits(sections_offset, nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), image.size()))
        incompatible(ERROR_BAD_EXE_FORMAT);
    const auto table = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!table.Size || table.Size % sizeof(RUNTIME_FUNCTION) ||
        !fits(table.VirtualAddress, table.Size, image.size())) incompatible(ERROR_BAD_EXE_FORMAT);
    size_t candidate = 0, candidates = 0;
    for (size_t n = 0; n < table.Size / sizeof(RUNTIME_FUNCTION); ++n) {
        const auto function = at<RUNTIME_FUNCTION>(image, table.VirtualAddress + n * sizeof(RUNTIME_FUNCTION));
        if (function.BeginAddress >= function.EndAddress || function.EndAddress > image.size())
            incompatible(ERROR_BAD_EXE_FORMAT);
        bool executable = false;
        for (size_t s = 0; s < nt.FileHeader.NumberOfSections; ++s) {
            const auto section = at<IMAGE_SECTION_HEADER>(image, sections_offset + s * sizeof(IMAGE_SECTION_HEADER));
            const size_t end = size_t{section.VirtualAddress} + section.Misc.VirtualSize;
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                function.BeginAddress >= section.VirtualAddress && function.EndAddress <= end && end <= image.size())
                executable = true;
        }
        if (!executable) continue;
        const auto body = image.subspan(function.BeginAddress, function.EndAddress - function.BeginAddress);
        if (contains(body, reader_body) && contains(body, reader_records)) {
            candidate = function.BeginAddress;
            ++candidates;
        }
    }
    if (!candidates) incompatible(ERROR_PROC_NOT_FOUND);
    if (candidates != 1) incompatible(ERROR_DUP_NAME);
    if (!fits(candidate, reader_entry.size(), image.size()) ||
        std::memcmp(image.data() + candidate, reader_entry.data(), reader_entry.size()) != 0)
        incompatible(ERROR_INVALID_DATA);
    return candidate;
}

void* reader_address(HMODULE module) {
    // Copy just headers, unwind records and executable sections. Reading an
    // entire loaded image would also touch legitimate no-access section gaps.
    std::vector<uint8_t> image(4096);
    copy_image_bytes(module, 0, image.data(), image.size());
    const auto dos = at<IMAGE_DOS_HEADER>(image, 0);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) incompatible(ERROR_BAD_EXE_FORMAT);
    const auto nt = at<IMAGE_NT_HEADERS64>(image, static_cast<size_t>(dos.e_lfanew));
    const size_t size = nt.OptionalHeader.SizeOfImage;
    if (size < 4096 || size > 256 * 1024 * 1024 || nt.OptionalHeader.SizeOfHeaders > size)
        incompatible(ERROR_BAD_EXE_FORMAT);
    image.resize(size);
    size_t nt_offset;
    headers(image, nt_offset);
    copy_image_bytes(module, 0, image.data(), nt.OptionalHeader.SizeOfHeaders);
    const auto sections_offset = nt_offset + sizeof(IMAGE_NT_HEADERS64);
    if (!nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96 ||
        !fits(sections_offset, nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), image.size()))
        incompatible(ERROR_BAD_EXE_FORMAT);
    for (size_t s = 0; s < nt.FileHeader.NumberOfSections; ++s) {
        const auto section = at<IMAGE_SECTION_HEADER>(image, sections_offset + s * sizeof(IMAGE_SECTION_HEADER));
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (!fits(section.VirtualAddress, section.Misc.VirtualSize, size)) incompatible(ERROR_BAD_EXE_FORMAT);
        copy_image_bytes(module, section.VirtualAddress, image.data() + section.VirtualAddress, section.Misc.VirtualSize);
    }
    const auto table = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!fits(table.VirtualAddress, table.Size, size)) incompatible(ERROR_BAD_EXE_FORMAT);
    copy_image_bytes(module, table.VirtualAddress, image.data() + table.VirtualAddress, table.Size);
    return reinterpret_cast<uint8_t*>(module) + reader_offset(image);
}
}
