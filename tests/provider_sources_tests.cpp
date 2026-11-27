#include <owh/overwolf/provider_sources.hpp>
#include <owh/platform/result.hpp>
#include <windows.h>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>
using namespace overwolf_hook;
namespace {
template<class T> void put(std::vector<uint8_t> &image, size_t offset, const T &value) {
    std::memcpy(image.data() + offset, &value, sizeof(value));
}
std::vector<uint8_t> fixture(uint32_t code, uint32_t global) {
    std::vector<uint8_t> image(0x8000);
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x80; put(image,0,dos);
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64; nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    nt.OptionalHeader.SizeOfHeaders = 0x1000;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = {0x1000,0x200};
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {0x1800,sizeof(RUNTIME_FUNCTION)};
    put(image,0x80,nt);
    IMAGE_SECTION_HEADER text{}; text.VirtualAddress = 0x2000; text.Misc.VirtualSize = 0x3000;
    text.Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    IMAGE_SECTION_HEADER data{}; data.VirtualAddress = 0x6000; data.Misc.VirtualSize = 0x1000;
    data.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    put(image,0x80+sizeof(nt),text); put(image,0x80+sizeof(nt)+sizeof(text),data);
    IMAGE_EXPORT_DIRECTORY exports{};
    exports.NumberOfFunctions = exports.NumberOfNames = 1;
    exports.AddressOfFunctions = 0x1100; exports.AddressOfNames = 0x1110; exports.AddressOfNameOrdinals = 0x1120;
    put(image,0x1000,exports); put(image,0x1100,code); put(image,0x1110,uint32_t{0x1140});
    put(image,0x1120,uint16_t{0}); std::memcpy(image.data()+0x1140,"GetDBinfo",10);
    put(image,0x1800,RUNTIME_FUNCTION{code,code+0x100,0x1900});
    const uint8_t controller[]{0x48,0x8b,0x7a,0x18};
    const uint8_t records[]{0x48,0x6b,0xcb,0x70,0x49,0x03,0xc8,0x4c,0x8d,0x41,0x20,0x49,0x83,0x78};
    std::memcpy(image.data()+code+0x20,controller,sizeof(controller));
    std::memcpy(image.data()+code+0x70,records,sizeof(records));
    image[code+0x40]=0x48; image[code+0x41]=0x3b; image[code+0x42]=0x15;
    put(image,code+0x43,static_cast<int32_t>(global-code-0x47));
    return image;
}
void rejects(const std::vector<uint8_t> &image) {
    try { overwolf::provider_registry_head_offset(image); assert(false); }
    catch(const Failure &failure) { assert(failure.result.status == OWH_INCOMPATIBLE); }
}
std::vector<std::byte> decode(std::string_view value, std::string_view key) {
    return overwolf::decode_provider_value(
        {reinterpret_cast<const std::byte *>(value.data()), value.size()},key);
}
}
int main() {
    auto image = fixture(0x2400,0x6010);
    assert(overwolf::provider_registry_head_offset(image) == 0x6010);
    assert(overwolf::provider_registry_head_offset(fixture(0x3800,0x6480)) == 0x6480);
    auto missing=image; missing[0x2420]=0; rejects(missing);
    auto ambiguous=image; std::memcpy(ambiguous.data()+0x2490,ambiguous.data()+0x2440,7); rejects(ambiguous);
    rejects(fixture(0x2400,0x3000)); // registry must be writable data, not code
    rejects(fixture(0x2400,0x7ff8)); // no full head/count storage
    const auto unchanged=decode("ordinary value","");
    assert(unchanged.size()==14 && std::memcmp(unchanged.data(),"ordinary value",14)==0);
    const std::string_view encoded="AAECAwQFBgcODRYbHncUDhsVEg==";
    const auto plain=decode(encoded,"fixture");
    const std::string_view expected("hello\0world",11);
    assert(plain.size()==expected.size());
    assert(std::memcmp(plain.data(),expected.data(),expected.size())==0);
    assert(decode("AAECAwQFBgc=","fixture") == decode("AAECAwQFBgc=",""));
    assert(decode("A!AEC AwQF\nBgcODRYbHncUDhsVEg==ignored","fixture") == plain);
    assert(decode("", "fixture").empty());
    std::cout << "PASS provider registry signatures, relocation, bounds and conditional value decode\n";
}
