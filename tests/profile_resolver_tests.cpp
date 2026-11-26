#include <owh/overwolf/signatures/lifecycle_targets.hpp>
#include <owh/overwolf/signatures/overlay_targets.hpp>
#include <owh/overwolf/signatures/resolver.hpp>
#include <owh/platform/pe_image.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {
using overwolf_hook::overwolf::ResolvedProfile;
constexpr uint64_t base = 0x180000000ull;
constexpr size_t image_size = 0x40000;
constexpr DWORD exception_rva = 0x5000;
constexpr DWORD export_rva = 0x6000;
constexpr DWORD names_rva = 0x6200;
constexpr DWORD ordinals_rva = 0x6300;
constexpr DWORD functions_rva = 0x6400;
constexpr DWORD strings_rva = 0x6500;
constexpr DWORD global_rva = 0x30000;

struct FunctionSpec {
    const char *export_name;
    std::span<const int16_t> first;
    std::span<const int16_t> second;
    std::span<const uint8_t> entry;
    DWORD rva;
    bool leaf = false;
};
template <size_t N> std::span<const int16_t> view(const std::array<int16_t, N> &v) { return v; }
template <size_t N> std::span<const uint8_t> view(const std::array<uint8_t, N> &v) { return v; }

void put(std::vector<uint8_t> &image, size_t offset, const void *data, size_t size) {
    if (offset > image.size() || size > image.size() - offset)
        throw 1;
    std::memcpy(image.data() + offset, data, size);
}
template <class T> void put(std::vector<uint8_t> &image, size_t offset, const T &value) {
    put(image, offset, &value, sizeof(value));
}
void apply_pattern(std::vector<uint8_t> &image, DWORD at, std::span<const int16_t> pattern) {
    for (size_t i = 0; i < pattern.size(); ++i)
        image[at + i] = pattern[i] < 0 ? 0x90 : static_cast<uint8_t>(pattern[i]);
}

std::vector<uint8_t> build_image(bool duplicate_run = false, bool omit_source = false,
                                 bool break_leaf = false, bool nonexec_source = false) {
    using namespace overwolf_hook::overwolf::signatures::overlay;
    using namespace overwolf_hook::overwolf::signatures::lifecycle;
    std::vector<uint8_t> image(image_size, 0);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    put(image, 0, dos);
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 4;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    nt.OptionalHeader.SizeOfHeaders = 0x1000;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {
        exception_rva, 10 * sizeof(RUNTIME_FUNCTION)};
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = {export_rva, 0x400};
    put(image, dos.e_lfanew, nt);
    std::array<IMAGE_SECTION_HEADER, 4> sections{};
    const auto section = [](IMAGE_SECTION_HEADER &s, const char *name, DWORD rva, DWORD size) {
        std::memcpy(s.Name, name, std::min(strlen(name), sizeof(s.Name)));
        s.Misc.VirtualSize = size;
        s.VirtualAddress = rva;
        s.SizeOfRawData = size;
        s.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
    };
    section(sections[0], ".text", 0x10000, 0x10000);
    section(sections[1], ".pdata", exception_rva, 0x1000);
    section(sections[2], ".rdata", export_rva, 0x4000);
    section(sections[3], ".data", 0x20000, 0x10000);
    sections[3].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    const size_t section_at = dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                              sizeof(IMAGE_OPTIONAL_HEADER64);
    put(image, section_at, sections.data(), sizeof(sections));

    using namespace overwolf_hook::overwolf::signatures::overlay;
    using namespace overwolf_hook::overwolf::signatures::lifecycle;
    std::array<FunctionSpec, 10> specs{
        {{source_paint_export, view(source_paint_anchor_1), view(source_paint_anchor_2),
          view(source_paint_entry), 0x10000},
         {accelerated_paint_observer_export, view(accelerated_paint_observer_anchor_1),
          view(accelerated_paint_observer_anchor_2), view(accelerated_paint_observer_entry),
          0x10200},
         {set_position_export, view(set_position_anchor_1), view(set_position_anchor_2),
          view(set_position_entry), 0x10400, true},
         {nullptr, view(register_window_anchor_1), view(register_window_anchor_2),
          view(register_window_entry), 0x10600},
         {nullptr, view(refresh_windows_anchor_1), view(refresh_windows_anchor_2),
          view(refresh_windows_entry), 0x10800},
         {agent_run_export, view(agent_run_anchor_1), view(agent_run_anchor_2),
          view(agent_run_entry), 0x10a00},
         {agent_clear_export, view(agent_clear_anchor_1), view(agent_clear_anchor_2),
          view(agent_clear_entry), 0x10c00},
         {agent_unhook_export, view(agent_unhook_anchor_1), view(agent_unhook_anchor_2),
          view(agent_unhook_entry), 0x10e00},
         {nullptr, view(main_agent_init_body_anchor_1), view(main_agent_init_body_anchor_2),
          view(main_agent_init_body_entry), 0x11000},
         {nullptr, view(main_agent_clear_body_anchor_1), view(main_agent_clear_body_anchor_2),
          view(main_agent_clear_body_entry), 0x11200}}};
    if (nonexec_source)
        specs[0].rva = 0x22000;
    std::vector<RUNTIME_FUNCTION> unwind;
    std::vector<std::pair<std::string_view, DWORD>> exports;
    for (const auto &spec : specs) {
        std::array<uint8_t, 0x100> body{};
        body.fill(0x90);
        const DWORD first_at = spec.leaf ? 0 : 16;
        apply_pattern(image, spec.rva + first_at, spec.first);
        std::copy(spec.entry.begin(), spec.entry.end(), image.begin() + spec.rva);
        const DWORD second_at = spec.leaf ? 32 : 64;
        apply_pattern(image, spec.rva + second_at, spec.second);
        if (spec.leaf) {
            std::copy(spec.entry.begin(), spec.entry.end(), image.begin() + spec.rva);
            if (break_leaf)
                image[spec.rva + second_at + spec.second.size() - 1] = 0x90;
            image[spec.rva + second_at + spec.second.size() - 1] = break_leaf ? 0x90 : 0xc3;
        } else if (spec.rva == 0x10000 || spec.rva == 0x10200) {
            const DWORD load = spec.rva + first_at + 3;
            const int32_t disp = static_cast<int32_t>(global_rva) - static_cast<int32_t>(load + 7);
            put(image, load + 3, disp);
        } else if (spec.rva == 0x10a00) {
            const DWORD load = spec.rva + second_at;
            const int32_t disp = static_cast<int32_t>(global_rva) - static_cast<int32_t>(load + 7);
            put(image, load + 3, disp);
        }
        if (!spec.leaf)
            unwind.push_back({spec.rva, spec.rva + 0x100, 0});
        if (spec.export_name)
            exports.emplace_back(spec.export_name, spec.rva);
    }
    if (omit_source)
        image[specs[0].rva + 16] ^= 0xff;
    if (duplicate_run)
        unwind.push_back({specs[5].rva, specs[5].rva + 0x100, 0});
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size =
        static_cast<DWORD>(unwind.size() * sizeof(RUNTIME_FUNCTION));
    put(image, dos.e_lfanew, nt);
    put(image, exception_rva, unwind.data(), unwind.size() * sizeof(RUNTIME_FUNCTION));

    IMAGE_EXPORT_DIRECTORY directory{};
    directory.NumberOfNames = static_cast<DWORD>(exports.size());
    directory.NumberOfFunctions = static_cast<DWORD>(exports.size());
    directory.AddressOfNames = names_rva;
    directory.AddressOfNameOrdinals = ordinals_rva;
    directory.AddressOfFunctions = functions_rva;
    put(image, export_rva, directory);
    DWORD cursor = strings_rva;
    for (DWORD i = 0; i < exports.size(); ++i) {
        const auto [name, rva] = exports[i];
        put(image, names_rva + i * sizeof(DWORD), cursor);
        put(image, ordinals_rva + i * sizeof(WORD), static_cast<WORD>(i));
        put(image, functions_rva + i * sizeof(DWORD), rva);
        put(image, cursor, name.data(), name.size());
        image[cursor + name.size()] = 0;
        cursor += static_cast<DWORD>(name.size() + 1);
    }
    return image;
}

bool check(std::string_view label, const ResolvedProfile &value, bool common, bool pixel_buffer, bool shared_texture) {
    const bool pass = value.common == common && value.pixel_buffer_source == pixel_buffer && value.shared_texture_source == shared_texture;
    std::cout << (pass ? "PASS " : "FAIL ") << label << " (actual " << value.common << ','
              << value.pixel_buffer_source << ',' << value.shared_texture_source << ")\n";
    return pass;
}
} // namespace

int main() {
    const auto good = build_image();
    IMAGE_NT_HEADERS64 parsed_headers{};
    size_t parsed_nt = 0;
    bool ok = overwolf_hook::platform::pe::mapped_headers(good, parsed_headers, parsed_nt);
    const std::array<int16_t, 3> wildcard{0x48, -1, 0x90};
    const std::array<uint8_t, 7> pattern_bytes{0, 0x48, 0x11, 0x90, 0x48, 0x22, 0x90};
    size_t first_match = 99;
    ok = overwolf_hook::platform::pe::count_pattern(pattern_bytes, wildcard, &first_match) == 2 &&
         first_match == 1 && ok;
    IMAGE_NT_HEADERS64 raw_nt{};
    raw_nt.OptionalHeader.SizeOfHeaders = 0x200;
    raw_nt.FileHeader.NumberOfSections = 1;
    std::array<uint8_t, 0x500> raw{};
    IMAGE_SECTION_HEADER raw_section{};
    raw_section.VirtualAddress = 0x1000;
    raw_section.SizeOfRawData = 0x100;
    raw_section.PointerToRawData = 0x200;
    std::memcpy(raw.data() + 0x100, &raw_section, sizeof(raw_section));
    size_t raw_offset = 0;
    ok = overwolf_hook::platform::pe::rva_offset(raw, raw_nt, 0x100, 0x1010, 8, raw_offset) &&
         raw_offset == 0x210 &&
         !overwolf_hook::platform::pe::rva_offset(raw, raw_nt, 0x100, 0x1100, 1, raw_offset) &&
         ok;
    const auto valid = overwolf_hook::overwolf::resolve_profile_image(good, base);
    ok = check("unique prepared targets resolve", valid, true, true, true) &&
              valid.agent_global_slot == base + global_rva &&
              valid.source_global == base + global_rva && valid.set_position == base + 0x10400 &&
              valid.agent_init == base + 0x11000;
    // Exercise the production loaded-module path with an anonymous image: no
    // reviewed filename, version or hash exists for this allocation.
    auto *loaded = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, good.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!loaded)
        return 1;
    std::copy(good.begin(), good.end(), loaded);
    ok = check("unreviewed loaded image resolves without a version/hash gate",
               overwolf_hook::overwolf::resolve_loaded_profile(reinterpret_cast<HMODULE>(loaded)),
               true, true, true) &&
         ok;
    const auto broken_loaded = build_image(true);
    std::copy(broken_loaded.begin(), broken_loaded.end(), loaded);
    ok = check("unreviewed loaded image still rejects ambiguous required targets",
               overwolf_hook::overwolf::resolve_loaded_profile(reinterpret_cast<HMODULE>(loaded)),
               false, true, false) &&
         ok;
    VirtualFree(loaded, 0, MEM_RELEASE);
    const auto missing = build_image(false, true);
    ok = check("missing pixel-buffer source is isolated",
               overwolf_hook::overwolf::resolve_profile_image(missing, base), true, false, true) &&
         ok;
    const auto ambiguous = build_image(true);
    ok = check("ambiguous lifecycle target is rejected",
               overwolf_hook::overwolf::resolve_profile_image(ambiguous, base), false, true,
               false) &&
         ok;
    const auto bad_leaf = build_image(false, false, true);
    ok =
        check("malformed leaf is rejected",
              overwolf_hook::overwolf::resolve_profile_image(bad_leaf, base), true, false, false) &&
        ok;
    const auto nonexec = build_image(false, false, false, true);
    ok = check("targets in non-executable sections are rejected",
               overwolf_hook::overwolf::resolve_profile_image(nonexec, base), true, false, true) &&
         ok;
    return ok ? 0 : 1;
}
