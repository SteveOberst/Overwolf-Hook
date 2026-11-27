#include <cstring>
#include <iostream>
#include <owh/overwolf/signatures/event_resolver.hpp>
#include <owh/platform/result.hpp>
#include <stdexcept>
#include <vector>

#define REQUIRE(test)                                                                              \
    do {                                                                                           \
        if (!(test))                                                                               \
            throw std::runtime_error(#test);                                                       \
    } while (false)
namespace {
using namespace overwolf_hook;
using overwolf_hook::overwolf::signatures::events::event_dispatcher_offset;
template <typename T> void put(std::vector<uint8_t> &image, size_t offset, const T &value) {
    std::memcpy(image.data() + offset, &value, sizeof(value));
}
void bytes(std::vector<uint8_t> &image, size_t offset, const char *hex) {
    while (*hex) {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        image.at(offset++) = static_cast<uint8_t>((digit(hex[0]) << 4) | digit(hex[1]));
        hex += 2;
    }
}
std::vector<uint8_t> fixture(uint32_t entry) {
    std::vector<uint8_t> image(0x8000);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    put(image, 0, dos);
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 1;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    nt.OptionalHeader.SizeOfHeaders = 0x1000;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {0x1000,
                                                                        sizeof(RUNTIME_FUNCTION)};
    put(image, 0x80, nt);
    IMAGE_SECTION_HEADER section{};
    section.VirtualAddress = 0x2000;
    section.Misc.VirtualSize = 0x5000;
    section.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    put(image, 0x80 + sizeof(nt), section);
    put(image, 0x1000, RUNTIME_FUNCTION{entry, entry + 0x100, 0x1800});
    // Literal code fragments from the reviewed dispatcher, independent of the
    // matcher declarations. No function RVA or anchor-to-entry offset is fixed.
    bytes(image, entry, "48895c2408488974241048897c241841564883ec30");
    bytes(image, entry + 0x40, "4c69cbfffc00004d03c8498d89ff000000");
    bytes(image, entry + 0xb0, "8944242848894c2420448bc38b4e0841ffd2");
    return image;
}
void rejected(const std::vector<uint8_t> &image, DWORD reason) {
    try {
        event_dispatcher_offset(image);
        throw std::runtime_error("accepted incompatible image");
    } catch (const Failure &failure) {
        REQUIRE(failure.result.status == OWH_INCOMPATIBLE && failure.result.native_code == reason);
    }
}
} // namespace
int main() {
    try {
        auto image = fixture(0x2400);
        REQUIRE(event_dispatcher_offset(image) == 0x2400);
        image = fixture(0x4000);
        // Move the record anchor within its unwind body; unused bytes may vary.
        std::memcpy(image.data() + 0x4060, image.data() + 0x4040, 17);
        std::memset(image.data() + 0x4040, 0x90, 17);
        bytes(image, 0x4090, "12345678");
        REQUIRE(event_dispatcher_offset(image) == 0x4000);
        image[0x4000] = 0xe9; // Another hook/changed entry: body still matches.
        rejected(image, ERROR_INVALID_DATA);
        image = fixture(0x2400);
        image[0x2400 + 0x40 + 3] ^= 1; // Record size changed.
        rejected(image, ERROR_PROC_NOT_FOUND);
        image = fixture(0x2400);
        image[0x2400 + 0xb0 + 14] ^= 1; // Delegate channel layout changed.
        rejected(image, ERROR_PROC_NOT_FOUND);
        image = fixture(0x2400);
        // An anchor outside the candidate's unwind extent cannot validate it.
        put(image, 0x1000, RUNTIME_FUNCTION{0x2400, 0x24a0, 0x1800});
        rejected(image, ERROR_PROC_NOT_FOUND);
        image = fixture(0x2400);
        std::memcpy(image.data() + 0x4400, image.data() + 0x2400, 0x100);
        put(image, 0x1000 + sizeof(RUNTIME_FUNCTION), RUNTIME_FUNCTION{0x4400, 0x4500, 0x1800});
        put(image,
            0x80 + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
                IMAGE_DIRECTORY_ENTRY_EXCEPTION * sizeof(IMAGE_DATA_DIRECTORY),
            IMAGE_DATA_DIRECTORY{0x1000, 2 * sizeof(RUNTIME_FUNCTION)});
        rejected(image, ERROR_DUP_NAME);
        image = fixture(0x2400);
        put(image,
            0x80 + sizeof(IMAGE_NT_HEADERS64) + offsetof(IMAGE_SECTION_HEADER, Characteristics),
            DWORD{IMAGE_SCN_MEM_READ});
        rejected(image, ERROR_PROC_NOT_FOUND); // Data sections cannot identify a function.
        image = fixture(0x2400);
        image.resize(150);
        rejected(image, ERROR_BAD_EXE_FORMAT);
        image = fixture(0x2400);
        put(image, 0x1000, RUNTIME_FUNCTION{0x2400, 0x9000, 0x1800});
        rejected(image, ERROR_BAD_EXE_FORMAT);
        std::cout << "PASS relocated unique dispatcher, moved body anchors, "
                     "ambiguous/missing/layout/entry guards, malformed PE\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
}
