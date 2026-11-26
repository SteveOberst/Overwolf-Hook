#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <windows.h>

namespace overwolf_hook::platform::pe {
enum class Layout { raw_file, mapped_image };

inline bool fits(size_t offset, size_t length, size_t total) noexcept {
    return offset <= total && length <= total - offset;
}

template <class T> inline bool read(std::span<const uint8_t> bytes, size_t offset, T &value) noexcept {
    if (!fits(offset, sizeof(T), bytes.size()))
        return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}

inline bool section_table(std::span<const uint8_t> bytes, size_t nt_offset,
                          const IMAGE_NT_HEADERS64 &nt, size_t &offset) noexcept {
    const size_t optional = nt_offset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader);
    if (optional < nt_offset || !fits(optional, nt.FileHeader.SizeOfOptionalHeader, bytes.size()))
        return false;
    offset = optional + nt.FileHeader.SizeOfOptionalHeader;
    return nt.FileHeader.NumberOfSections <= 96 &&
           fits(offset, size_t{nt.FileHeader.NumberOfSections} * sizeof(IMAGE_SECTION_HEADER),
                bytes.size());
}

inline bool mapped_headers(std::span<const uint8_t> bytes, IMAGE_NT_HEADERS64 &nt,
                           size_t &nt_offset) noexcept {
    IMAGE_DOS_HEADER dos{};
    if (!read(bytes, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        return false;
    nt_offset = static_cast<size_t>(dos.e_lfanew);
    if (!read(bytes, nt_offset, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage > bytes.size())
        return false;
    size_t sections = 0;
    return section_table(bytes, nt_offset, nt, sections);
}

inline bool rva_offset(std::span<const uint8_t> raw, const IMAGE_NT_HEADERS64 &nt,
                       size_t sections_offset, DWORD rva, size_t length, size_t &offset) noexcept {
    if (rva < nt.OptionalHeader.SizeOfHeaders &&
        length <= nt.OptionalHeader.SizeOfHeaders - rva && fits(rva, length, raw.size())) {
        offset = rva;
        return true;
    }
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!read(raw, sections_offset + size_t{i} * sizeof(section), section))
            return false;
        if (rva < section.VirtualAddress)
            continue;
        const size_t delta = size_t{rva} - section.VirtualAddress;
        if (!fits(delta, length, section.SizeOfRawData))
            continue;
        const size_t raw_offset = size_t{section.PointerToRawData} + delta;
        if (raw_offset < section.PointerToRawData || !fits(raw_offset, length, raw.size()))
            return false;
        offset = raw_offset;
        return true;
    }
    return false;
}

inline std::optional<std::span<const uint8_t>> mapped_rva(std::span<const uint8_t> mapped,
                                                           const IMAGE_NT_HEADERS64 &nt,
                                                           DWORD rva, size_t length) noexcept {
    if (rva > nt.OptionalHeader.SizeOfImage || length > nt.OptionalHeader.SizeOfImage - rva ||
        !fits(rva, length, mapped.size()))
        return std::nullopt;
    return mapped.subspan(rva, length);
}

inline bool export_rva(std::span<const uint8_t> bytes, Layout layout,
                       const IMAGE_NT_HEADERS64 &nt, size_t sections_offset,
                       std::string_view wanted, DWORD &result) noexcept {
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto offset_of = [&](DWORD rva, size_t length, size_t &offset) {
        if (layout == Layout::raw_file)
            return rva_offset(bytes, nt, sections_offset, rva, length, offset);
        offset = rva;
        return fits(rva, length, bytes.size()) && rva <= nt.OptionalHeader.SizeOfImage &&
               length <= nt.OptionalHeader.SizeOfImage - rva;
    };
    size_t directory_offset = 0;
    IMAGE_EXPORT_DIRECTORY exports{};
    if (!directory.VirtualAddress || directory.Size < sizeof(exports) ||
        !offset_of(directory.VirtualAddress, directory.Size, directory_offset) ||
        !read(bytes, directory_offset, exports) || !exports.NumberOfNames ||
        exports.NumberOfNames > 65536 || !exports.NumberOfFunctions ||
        exports.NumberOfFunctions > 65536)
        return false;
    size_t name_table = 0, ordinal_table = 0, function_table = 0;
    if (!offset_of(exports.AddressOfNames, size_t{exports.NumberOfNames} * sizeof(DWORD), name_table) ||
        !offset_of(exports.AddressOfNameOrdinals,
                   size_t{exports.NumberOfNames} * sizeof(WORD), ordinal_table) ||
        !offset_of(exports.AddressOfFunctions,
                   size_t{exports.NumberOfFunctions} * sizeof(DWORD), function_table))
        return false;
    size_t matches = 0;
    DWORD found = 0;
    for (DWORD i = 0; i < exports.NumberOfNames; ++i) {
        DWORD name_rva = 0;
        WORD ordinal = 0;
        if (!read(bytes, name_table + size_t{i} * sizeof(name_rva), name_rva) ||
            !read(bytes, ordinal_table + size_t{i} * sizeof(ordinal), ordinal) ||
            ordinal >= exports.NumberOfFunctions)
            return false;
        size_t name_offset = 0;
        if (!offset_of(name_rva, 1, name_offset))
            return false;
        const char *name = reinterpret_cast<const char *>(bytes.data() + name_offset);
        const auto remaining = bytes.subspan(name_offset);
        const auto *terminator = static_cast<const char *>(std::memchr(name, 0, remaining.size()));
        if (!terminator)
            return false;
        if (std::string_view(name, static_cast<size_t>(terminator - name)) != wanted)
            continue;
        if (!read(bytes, function_table + size_t{ordinal} * sizeof(found), found))
            return false;
        ++matches;
    }
    const uint64_t end = uint64_t{directory.VirtualAddress} + directory.Size;
    if (matches != 1 || !found || found >= nt.OptionalHeader.SizeOfImage ||
        (found >= directory.VirtualAddress && found < end))
        return false;
    result = found;
    return true;
}

template <size_t N>
inline bool pattern_at(std::span<const uint8_t> bytes, size_t offset,
                       const std::array<int16_t, N> &pattern) noexcept {
    if (!fits(offset, N, bytes.size()))
        return false;
    for (size_t i = 0; i < N; ++i)
        if (pattern[i] >= 0 && pattern[i] != bytes[offset + i])
            return false;
    return true;
}

template <size_t N>
inline size_t count_pattern(std::span<const uint8_t> bytes,
                            const std::array<int16_t, N> &pattern,
                            size_t *first = nullptr) noexcept {
    if (N == 0 || bytes.size() < N)
        return 0;
    size_t count = 0;
    for (size_t offset = 0; offset <= bytes.size() - N; ++offset) {
        if (!pattern_at(bytes, offset, pattern))
            continue;
        if (!count && first)
            *first = offset;
        ++count;
    }
    return count;
}
} // namespace overwolf_hook::platform::pe
