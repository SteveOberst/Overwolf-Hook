#include "capture/overwolf/reader.hpp"
#include "transport/channel.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)
namespace {
using namespace overwolf_hook;
using overwolf_hook::overwolf::reader_offset;
template<typename T> void put(std::vector<uint8_t>& image, size_t offset, const T& value) {
    std::memcpy(image.data() + offset, &value, sizeof(value));
}
void bytes(std::vector<uint8_t>& image, size_t offset, const char* hex) {
    while (*hex) {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        image.at(offset++) = static_cast<uint8_t>((digit(hex[0]) << 4) | digit(hex[1]));
        hex += 2;
    }
}
std::vector<uint8_t> fixture(uint32_t entry) {
    std::vector<uint8_t> image(0x8000);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x80;
    put(image, 0, dos);
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE; nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 1; nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size()); nt.OptionalHeader.SizeOfHeaders = 0x1000;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {0x1000, sizeof(RUNTIME_FUNCTION)};
    put(image, 0x80, nt);
    IMAGE_SECTION_HEADER section{};
    section.VirtualAddress = 0x2000; section.Misc.VirtualSize = 0x5000;
    section.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    put(image, 0x80 + sizeof(nt), section);
    put(image, 0x1000, RUNTIME_FUNCTION{entry, entry + 0x100, 0x1800});
    // Literal code fragments from the reference artifact, independent of the
    // matcher declarations. Move the function and change its RIP displacement.
    bytes(image, entry, "4c894424184889542410");
    bytes(image, entry + 0x40, "4c8bfa488bf1488b02488942084c8b69384c896c2420488d05a8d2020048894424584c896c246066c7442468000032c08885c0fc00004d85ed");
    bytes(image, entry + 0xb0, "41690cbefffc00004103cc488b4638488b5030");
    return image;
}
void rejected(const std::vector<uint8_t>& image, DWORD reason) {
    try { reader_offset(image); throw std::runtime_error("accepted incompatible image"); }
    catch (const Failure& failure) {
        REQUIRE(failure.result.status == OWH_INCOMPATIBLE && failure.result.native_error == reason);
    }
}
}
int main() {
    try {
        auto image = fixture(0x2400);
        REQUIRE(reader_offset(image) == 0x2400);
        image = fixture(0x4000);
        bytes(image, 0x4000 + 0x40 + 25, "12345678");
        REQUIRE(reader_offset(image) == 0x4000);
        image[0x4000] = 0xe9; // Another hook/changed entry: body still matches.
        rejected(image, ERROR_INVALID_DATA);
        image = fixture(0x2400); image[0x2400 + 0xb0 + 4] ^= 1; // Record size changed.
        rejected(image, ERROR_PROC_NOT_FOUND);
        image = fixture(0x2400); image[0x2400 + 0x40 + 16] ^= 1; // Reader object layout changed.
        rejected(image, ERROR_PROC_NOT_FOUND);
        image = fixture(0x2400);
        std::memcpy(image.data() + 0x4400, image.data() + 0x2400, 0x100);
        put(image, 0x1000 + sizeof(RUNTIME_FUNCTION), RUNTIME_FUNCTION{0x4400, 0x4500, 0x1800});
        put(image, 0x80 + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
            offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + IMAGE_DIRECTORY_ENTRY_EXCEPTION * sizeof(IMAGE_DATA_DIRECTORY),
            IMAGE_DATA_DIRECTORY{0x1000, 2 * sizeof(RUNTIME_FUNCTION)});
        rejected(image, ERROR_DUP_NAME);
        image = fixture(0x2400);
        put(image, 0x80 + sizeof(IMAGE_NT_HEADERS64) + offsetof(IMAGE_SECTION_HEADER, Characteristics), DWORD{IMAGE_SCN_MEM_READ});
        rejected(image, ERROR_PROC_NOT_FOUND); // Data sections cannot identify a function.
        image = fixture(0x2400); image.resize(150);
        rejected(image, ERROR_BAD_EXE_FORMAT);
        image = fixture(0x2400); put(image, 0x1000, RUNTIME_FUNCTION{0x2400, 0x9000, 0x1800});
        rejected(image, ERROR_BAD_EXE_FORMAT);
        std::cout << "PASS relocated unique reader, masked displacement, ambiguous/missing/layout/entry guards, malformed PE\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL " << failure.what() << '\n'; return 1;
    }
}
