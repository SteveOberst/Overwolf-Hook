#include <owh/overwolf/signatures/lifecycle_targets.hpp>
#include <owh/overwolf/signatures/overlay_targets.hpp>
#include <owh/overwolf/signatures/resolver.hpp>
#include <owh/platform/pe_image.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace overwolf_hook::overwolf {
namespace {
template <size_t N>
bool match_offset(std::span<const uint8_t> bytes, const std::array<int16_t, N> &pattern,
                  size_t &found) {
    return platform::pe::count_pattern(bytes, pattern, &found) != 0;
}
template <typename T> bool load_at(std::span<const uint8_t> image, size_t offset, T &value) {
    return platform::pe::read(image, offset, value);
}

bool executable_extent(std::span<const uint8_t> image, const IMAGE_NT_HEADERS64 &nt, DWORD begin,
                       DWORD end) {
    if (begin >= end || end > nt.OptionalHeader.SizeOfImage || end > image.size())
        return false;
    IMAGE_DOS_HEADER dos{};
    if (!load_at(image, 0, dos) || dos.e_lfanew < 0)
        return false;
    size_t section_table = 0;
    if (!platform::pe::section_table(image, static_cast<size_t>(dos.e_lfanew), nt, section_table))
        return false;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!load_at(image, section_table + size_t{i} * sizeof(section), section))
            return false;
        const size_t section_begin = section.VirtualAddress;
        const size_t section_size =
            std::max<size_t>(section.Misc.VirtualSize, section.SizeOfRawData);
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && begin >= section_begin &&
            begin - section_begin < section_size && end - section_begin <= section_size)
            return true;
    }
    return false;
}

bool export_rva(std::span<const uint8_t> image, const IMAGE_NT_HEADERS64 &nt,
                std::string_view wanted, DWORD &rva) {
    IMAGE_DOS_HEADER dos{};
    size_t sections = 0;
    return load_at(image, 0, dos) && dos.e_lfanew >= 0 &&
           platform::pe::section_table(image, static_cast<size_t>(dos.e_lfanew), nt, sections) &&
           platform::pe::export_rva(image, platform::pe::Layout::mapped_image, nt, sections, wanted,
                                    rva);
}

template <size_t N1, size_t N2, size_t NE>
bool resolve_target(std::span<const uint8_t> image, const IMAGE_NT_HEADERS64 &nt,
                    const std::array<int16_t, N1> &first, const std::array<int16_t, N2> &second,
                    const std::array<uint8_t, NE> &entry, const char *export_name, bool leaf,
                    uint64_t &address, size_t *first_anchor_offset = nullptr,
                    size_t *second_anchor_offset = nullptr) {
    auto check_leaf = [&](DWORD exported) {
        if (exported >= image.size() || entry.size() > image.size() - exported)
            return false;
        IMAGE_DOS_HEADER dos{};
        if (!load_at(image, 0, dos) || dos.e_lfanew < 0)
            return false;
        size_t body_end = exported;
        size_t section_table = 0;
        if (!platform::pe::section_table(image, static_cast<size_t>(dos.e_lfanew), nt,
                                         section_table))
            return false;
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!load_at(image, section_table + size_t{i} * sizeof(section), section))
                return false;
            const size_t start = section.VirtualAddress;
            const size_t extent = std::max<size_t>(section.Misc.VirtualSize, section.SizeOfRawData);
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && exported >= start &&
                exported - start < extent) {
                body_end = std::min<size_t>(image.size(), start + extent);
                break;
            }
        }
        if (body_end <= exported)
            return false;
        const auto body = image.subspan(exported, body_end - exported);
        size_t first_at = 0, second_at = 0;
        if (!match_offset(body, first, first_at) || first_at != 0 ||
            !match_offset(body, second, second_at) || second_at + second.size() > body.size() ||
            second.empty() || second.back() != 0xc3 || entry.size() > body.size() ||
            !std::equal(entry.begin(), entry.end(), body.begin()))
            return false;
        // This prepared set-position pattern is the complete leaf tail, including
        // its terminal RET. Do not infer instructions by scanning displacement bytes.
        return true;
    };
    if (leaf) {
        DWORD exported = 0;
        if (!export_name || !export_rva(image, nt, export_name, exported) || !check_leaf(exported))
            return false;
        address = exported;
        if (first_anchor_offset)
            *first_anchor_offset = 0;
        return true;
    }
    const auto table = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!table.VirtualAddress || !table.Size || table.Size % sizeof(RUNTIME_FUNCTION))
        return false;
    size_t count = 0;
    DWORD found = 0;
    for (size_t i = 0; i < table.Size / sizeof(RUNTIME_FUNCTION); ++i) {
        RUNTIME_FUNCTION fn{};
        if (!load_at(image, table.VirtualAddress + i * sizeof(fn), fn) ||
            fn.BeginAddress >= fn.EndAddress || fn.EndAddress > nt.OptionalHeader.SizeOfImage ||
            fn.EndAddress > image.size())
            return false;
        if (!executable_extent(image, nt, fn.BeginAddress, fn.EndAddress))
            continue;
        const auto body = image.subspan(fn.BeginAddress, fn.EndAddress - fn.BeginAddress);
        if (entry.size() > body.size())
            continue;
        size_t anchor_offset = 0;
        size_t second_offset = 0;
        if (!match_offset(body, first, anchor_offset) || !match_offset(body, second, second_offset))
            continue;
        if (!std::equal(entry.begin(), entry.end(), body.begin()))
            continue;
        if (export_name) {
            DWORD exported = 0;
            if (!export_rva(image, nt, export_name, exported) || exported != fn.BeginAddress)
                continue;
        }
        found = fn.BeginAddress;
        if (first_anchor_offset)
            *first_anchor_offset = anchor_offset;
        if (second_anchor_offset)
            *second_anchor_offset = second_offset;
        ++count;
    }
    if (count != 1)
        return false;
    address = found;
    return true;
}

bool copy_module(HMODULE module, std::vector<uint8_t> &image, IMAGE_NT_HEADERS64 &nt) {
    IMAGE_DOS_HEADER dos{};
    if (!load_at(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(module), 4096), 0,
                 dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0)
        return false;
    const auto *base = reinterpret_cast<const uint8_t *>(module);
    if (!load_at(std::span<const uint8_t>(base, 4096), static_cast<size_t>(dos.e_lfanew), nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION ||
        nt.OptionalHeader.SizeOfImage > 256u * 1024 * 1024 || nt.FileHeader.NumberOfSections > 96)
        return false;
    image.assign(nt.OptionalHeader.SizeOfImage, 0);
    if (nt.OptionalHeader.SizeOfHeaders > image.size())
        return false;
    std::memcpy(image.data(), base, nt.OptionalHeader.SizeOfHeaders);
    const size_t sections_at = static_cast<size_t>(dos.e_lfanew) +
                               offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                               nt.FileHeader.SizeOfOptionalHeader;
    for (size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!load_at(std::span<const uint8_t>(base,
                                              nt.OptionalHeader.SizeOfHeaders +
                                                  nt.FileHeader.NumberOfSections * sizeof(section)),
                     sections_at + i * sizeof(section), section))
            return false;
        if (!(section.Characteristics & IMAGE_SCN_MEM_READ))
            continue;
        const size_t bytes = section.Misc.VirtualSize;
        if (section.VirtualAddress > image.size() || bytes > image.size() - section.VirtualAddress)
            return false;
        std::memcpy(image.data() + section.VirtualAddress, base + section.VirtualAddress, bytes);
    }
    const auto exceptions = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    return exceptions.VirtualAddress && exceptions.Size &&
           exceptions.VirtualAddress <= image.size() &&
           exceptions.Size <= image.size() - exceptions.VirtualAddress;
}
} // namespace

ResolvedProfile resolve_profile_image(std::span<const uint8_t> image,
                                      uint64_t image_base) noexcept {
    ResolvedProfile result{};
    try {
        IMAGE_NT_HEADERS64 nt{};
        size_t nt_offset = 0;
        if (!platform::pe::mapped_headers(image, nt, nt_offset) ||
            nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION)
            return result;
        std::array<uint64_t, 8> resolved{};
        std::array<bool, 8> valid{};
        std::array<size_t, 8> first_anchor_offsets{};
        std::array<size_t, 8> second_anchor_offsets{};
        using namespace overwolf_hook::overwolf::signatures::overlay;
        const auto bytes = image;
        valid[0] = resolve_target(bytes, nt, source_paint_anchor_1, source_paint_anchor_2,
                                  source_paint_entry, source_paint_export, false, resolved[0],
                                  &first_anchor_offsets[0]);
        valid[1] = resolve_target(
            bytes, nt, accelerated_paint_observer_anchor_1, accelerated_paint_observer_anchor_2,
            accelerated_paint_observer_entry, accelerated_paint_observer_export, false, resolved[1],
            &first_anchor_offsets[1]);
        valid[2] = resolve_target(bytes, nt, set_position_anchor_1, set_position_anchor_2,
                                  set_position_entry, set_position_export, true, resolved[2],
                                  &first_anchor_offsets[2]);
        valid[3] = resolve_target(bytes, nt, register_window_anchor_1, register_window_anchor_2,
                                  register_window_entry, nullptr, false, resolved[3],
                                  &first_anchor_offsets[3]);
        valid[4] = resolve_target(bytes, nt, refresh_windows_anchor_1, refresh_windows_anchor_2,
                                  refresh_windows_entry, nullptr, false, resolved[4],
                                  &first_anchor_offsets[4]);
        valid[5] = resolve_target(bytes, nt, agent_run_anchor_1, agent_run_anchor_2,
                                  agent_run_entry, agent_run_export, false, resolved[5],
                                  &first_anchor_offsets[5], &second_anchor_offsets[5]);
        valid[6] =
            resolve_target(bytes, nt, agent_clear_anchor_1, agent_clear_anchor_2, agent_clear_entry,
                           agent_clear_export, false, resolved[6], &first_anchor_offsets[6]);
        valid[7] = resolve_target(bytes, nt, agent_unhook_anchor_1, agent_unhook_anchor_2,
                                  agent_unhook_entry, agent_unhook_export, false, resolved[7],
                                  &first_anchor_offsets[7]);
        using namespace overwolf_hook::overwolf::signatures::lifecycle;
        constexpr std::array<uint8_t, 5> init_entry{0x48, 0x89, 0x5c, 0x24, 0x18};
        constexpr std::array<uint8_t, 6> clear_entry{0x40, 0x53, 0x48, 0x83, 0xec, 0x20};
        uint64_t init_body = 0, clear_body = 0;
        const bool init_valid =
            resolve_target(bytes, nt, main_agent_init_body_anchor_1, main_agent_init_body_anchor_2,
                           init_entry, nullptr, false, init_body);
        const bool clear_valid =
            resolve_target(bytes, nt, main_agent_clear_body_anchor_1,
                           main_agent_clear_body_anchor_2, clear_entry, nullptr, false, clear_body);
        result.agent_init = init_valid ? image_base + init_body : 0;

        result.agent_clear = clear_valid ? image_base + clear_body : 0;

        result.source_paint = valid[0] ? image_base + resolved[0] : 0;
        result.accelerated_paint = valid[1] ? image_base + resolved[1] : 0;
        result.set_position = valid[2] ? image_base + resolved[2] : 0;
        result.register_window = valid[3] ? image_base + resolved[3] : 0;
        result.refresh_windows = valid[4] ? image_base + resolved[4] : 0;
        result.run_agent = valid[5] ? image_base + resolved[5] : 0;

        result.clear_agent = valid[6] ? image_base + resolved[6] : 0;

        result.unhook_agent = valid[7] ? image_base + resolved[7] : 0;

        int64_t root_global_rva = -1;
        if (valid[5]) {
            const size_t run_load =
                resolved[5] + second_anchor_offsets[5]; // 48 8B 0D disp32; next IP is +7.
            int32_t displacement = 0;
            if (run_load + 7 <= image.size()) {
                std::memcpy(&displacement, image.data() + run_load + 3, sizeof(displacement));
                root_global_rva = static_cast<int64_t>(run_load + 7) + displacement;
                if (root_global_rva >= 0 &&
                    static_cast<uint64_t>(root_global_rva) <= image.size() - sizeof(uint64_t))
                    result.agent_global_slot = image_base + static_cast<uint64_t>(root_global_rva);
            }
        }
        const auto rip_global = [&](size_t target, size_t offset,
                                    size_t instruction_bytes) -> int64_t {
            const size_t instruction = resolved[target] + offset;
            if (instruction + instruction_bytes > image.size() || instruction_bytes < 7)
                return -1;
            int32_t displacement = 0;
            std::memcpy(&displacement, image.data() + instruction + 3, sizeof(displacement));
            return static_cast<int64_t>(instruction + instruction_bytes) + displacement;
        };
        const int64_t source_global_rva =
            valid[0] ? rip_global(0, first_anchor_offsets[0] + 3, 7) : -1;
        const int64_t texture_global_rva =
            valid[1] ? rip_global(1, first_anchor_offsets[1] + 3, 7) : -1;
        const bool source_global_valid =
            source_global_rva >= 0 &&
            static_cast<uint64_t>(source_global_rva) <= image.size() - sizeof(uint64_t) &&
            (root_global_rva < 0 || source_global_rva == root_global_rva);
        const bool texture_global_valid =
            texture_global_rva >= 0 &&
            static_cast<uint64_t>(texture_global_rva) <= image.size() - sizeof(uint64_t) &&
            (root_global_rva < 0 || texture_global_rva == root_global_rva);
        if (root_global_rva >= 0)
            result.source_global = image_base + static_cast<uint64_t>(root_global_rva);
        result.pixel_buffer_source = valid[0] && source_global_valid && valid[2] && valid[3] && valid[4];
        result.shared_texture_source = valid[1] && texture_global_valid && result.agent_global_slot && valid[2] &&
                            valid[3] && valid[4];
        result.lifecycle = valid[5] && valid[6] && valid[7];
        result.common = result.lifecycle && valid[3] && valid[4] && init_valid && clear_valid &&
                        result.agent_global_slot;
        return result;
    } catch (...) {
        return {};
    }
}

ResolvedProfile resolve_loaded_profile(HMODULE agent) noexcept {
    if (!agent)
        return {};
    try {
        std::vector<uint8_t> image;
        IMAGE_NT_HEADERS64 nt{};
        if (!copy_module(agent, image, nt))
            return {};
        return resolve_profile_image(image, reinterpret_cast<uint64_t>(agent));
    } catch (...) {
        return {};
    }
}
} // namespace overwolf_hook::overwolf
