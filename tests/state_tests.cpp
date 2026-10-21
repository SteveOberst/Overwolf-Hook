#include <overwolf_hook/state.h>
#include <windows.h>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool test) { if (!test) throw std::runtime_error("state assertion failed"); }
template<typename T> void append(std::vector<unsigned char>& out, T value) {
    const auto p = reinterpret_cast<const unsigned char*>(&value);
    out.insert(out.end(), p, p + sizeof(value));
}
void name(std::vector<unsigned char>& out, const char* text) {
    out.insert(out.end(), text, text + std::strlen(text) + 1);
}
// Independently authored provider wire: metadata, timestamps, then length/reservation.
std::vector<unsigned char> database() {
    std::vector<unsigned char> out(4);
    name(out, "player"); append<uint32_t>(out, 2);
    name(out, "unknown_key"); append<uint32_t>(out, 8);
    name(out, "unwritten"); append<uint32_t>(out, 3);
    const auto end = static_cast<uint32_t>(out.size());
    std::memcpy(out.data(), &end, 4);
    append<uint64_t>(out, 123456789); append<uint64_t>(out, 0);
    append<uint32_t>(out, 4);
    out.insert(out.end(), {0xff, 0, 0x80, 0x42, 0, 0, 0, 0});
    append<uint32_t>(out, 0); out.insert(out.end(), 3, 0);
    return out;
}
struct Source {
    uint32_t channel = 2000000000u + GetCurrentProcessId();
    HANDLE mapping = nullptr, mutex = nullptr, notification = nullptr;
    void* view = nullptr;
    Source() {
        const auto prefix = L"ow_info_" + std::to_wstring(channel);
        mutex = CreateMutexW(nullptr, FALSE, (prefix + L"mtx").c_str());
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, prefix.c_str());
        notification = CreateEventW(nullptr, FALSE, FALSE, (prefix + L"evnt").c_str());
        require(mapping && mutex && notification && GetLastError() != ERROR_ALREADY_EXISTS);
        view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 4096);
        require(view != nullptr);
        write(database());
    }
    void write(const std::vector<unsigned char>& bytes) {
        require(bytes.size() <= 4096);
        require(WaitForSingleObject(mutex, 100) == WAIT_OBJECT_0);
        std::memset(view, 0, 4096);
        std::memcpy(view, bytes.data(), bytes.size());
        require(ReleaseMutex(mutex));
    }
    ~Source() {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
        if (notification) CloseHandle(notification);
    }
};
struct Copy {
    owh_state* state = nullptr;
    ~Copy() { owh_state_close(state); }
    owh_result read(uint32_t channel, uint32_t timeout = 100) {
        owh_state_close(state); state = nullptr;
        return owh_state_read(OWH_ABI_VERSION, channel, timeout, &state);
    }
};
void roundtrip() {
    Source source;
    SetEvent(source.notification);
    Copy first, second;
    require(first.read(source.channel).status == OWH_OK);
    require(second.read(source.channel).status == OWH_OK);
    require(owh_state_count(first.state) == 2);
    owh_state_field field{}, other{};
    require(owh_state_at(first.state, 0, &field).status == OWH_OK);
    require(owh_state_at(second.state, 0, &other).status == OWH_OK);
    require(std::string(field.category) == "player" && std::string(field.key) == "unknown_key");
    require(field.producer_ticks == 123456789 && field.size == 4 && field.reserved_bytes == 8);
    const unsigned char expected[]{0xff, 0, 0x80, 0x42};
    require(std::memcmp(field.bytes, expected, 4) == 0 && std::memcmp(other.bytes, expected, 4) == 0);
    require(owh_state_at(first.state, 1, &field).status == OWH_OK && field.size == 0 && field.producer_ticks == 0);
    require(owh_state_at(first.state, 2, &field).status == OWH_INVALID_ARGUMENT);
    // No notification was consumed by either read.
    require(WaitForSingleObject(source.notification, 0) == WAIT_OBJECT_0);
    require(WaitForSingleObject(source.notification, 0) == WAIT_TIMEOUT);
    auto changed = database();
    uint32_t metadata_end; std::memcpy(&metadata_end, changed.data(), 4);
    changed[metadata_end + 16 + 4] = 0x11;
    source.write(changed);
    require(second.read(source.channel).status == OWH_OK);
    require(owh_state_at(second.state, 0, &other).status == OWH_OK);
    require(*static_cast<const unsigned char*>(other.bytes) == 0x11);
    require(owh_state_at(first.state, 0, &field).status == OWH_OK);
    require(std::memcmp(field.bytes, expected, 4) == 0); // Owned previous copy.
}
void bounds() {
    Source source; Copy result;
    auto invalid = database();
    uint32_t end; std::memcpy(&end, invalid.data(), 4);
    uint32_t oversize = 9; std::memcpy(invalid.data() + end + 16, &oversize, 4);
    source.write(invalid);
    require(result.read(source.channel).status == OWH_CORRUPT && !result.state);
    source.write({0xff, 0xff, 0xff, 0xff});
    require(result.read(source.channel).status == OWH_CORRUPT && !result.state);
    std::vector<unsigned char> duplicate(4);
    name(duplicate, "x"); append<uint32_t>(duplicate, 2);
    name(duplicate, "same"); append<uint32_t>(duplicate, 0);
    name(duplicate, "same"); append<uint32_t>(duplicate, 0);
    const uint32_t boundary = static_cast<uint32_t>(duplicate.size());
    std::memcpy(duplicate.data(), &boundary, 4);
    source.write(duplicate);
    require(result.read(source.channel).status == OWH_CORRUPT && !result.state);
    std::vector<unsigned char> missing_terminator(300, 'a');
    uint32_t length = 300; std::memcpy(missing_terminator.data(), &length, 4);
    source.write(missing_terminator);
    require(result.read(source.channel).status == OWH_CORRUPT);
    auto huge_capacity = database(); uint32_t huge = 4 * 1024 * 1024;
    // First reservation starts after header + category + count + key.
    std::memcpy(huge_capacity.data() + 4 + 7 + 4 + 12, &huge, 4);
    source.write(huge_capacity);
    require(result.read(source.channel).status == OWH_CORRUPT);
}
void availability() {
    Source source; Copy result;
    require(result.read(source.channel + 1).status == OWH_NOT_FOUND && !result.state);
    require(owh_state_read(99, source.channel, 100, &result.state).status == OWH_INCOMPATIBLE);
    require(owh_state_read(OWH_ABI_VERSION, source.channel, 1001, &result.state).status == OWH_INVALID_ARGUMENT);
    source.write({4, 0, 0, 0});
    require(result.read(source.channel).status == OWH_OK && owh_state_count(result.state) == 0);
}
void lifecycle() {
    Copy result; uint32_t channel;
    {
        Source source; channel = source.channel;
        require(result.read(channel).status == OWH_OK);
    }
    // Holding a copied state does not pin the original mapping or mutex.
    const auto name = L"ow_info_" + std::to_wstring(channel);
    HANDLE stale = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    require(!stale && GetLastError() == ERROR_FILE_NOT_FOUND);
    owh_state_field field{};
    require(owh_state_at(result.state, 0, &field).status == OWH_OK && field.size == 4);
    require(result.read(channel).status == OWH_NOT_FOUND);
    {
        Source replacement;
        require(result.read(replacement.channel).status == OWH_OK);
    }
}
void contention() {
    Source source; Copy result;
    std::promise<void> locked, release;
    auto done = release.get_future();
    std::thread owner([&] {
        require(WaitForSingleObject(source.mutex, 100) == WAIT_OBJECT_0);
        locked.set_value(); done.wait(); ReleaseMutex(source.mutex);
    });
    locked.get_future().wait();
    const auto status = result.read(source.channel, 0).status;
    release.set_value(); owner.join();
    require(status == OWH_BUSY && !result.state);
    std::thread abandoned([&] { WaitForSingleObject(source.mutex, 100); });
    abandoned.join();
    require(result.read(source.channel).status == OWH_CORRUPT && !result.state);
    // Abandoned ownership was released during error unwinding.
    require(result.read(source.channel).status == OWH_OK);
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) return 2;
        const std::string test = argv[1];
        if (test == "jvm" && argc == 5) {
            Source source;
            std::string command = "\"" + std::string(argv[2]) + "\" --enable-native-access=ALL-UNNAMED \"" +
                argv[3] + "\" \"" + argv[4] + "\" " + std::to_string(source.channel);
            std::vector<char> mutable_command(command.begin(), command.end()); mutable_command.push_back(0);
            STARTUPINFOA startup{}; startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            require(CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));
            const auto waited = WaitForSingleObject(process.hProcess, 10000);
            DWORD exit = 1; GetExitCodeProcess(process.hProcess, &exit);
            if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            require(waited == WAIT_OBJECT_0 && exit == 0);
            return 0;
        }
        if (argc != 2) return 2;
        if (test == "roundtrip") roundtrip();
        else if (test == "bounds") bounds();
        else if (test == "availability") availability();
        else if (test == "lifecycle") lifecycle();
        else if (test == "contention") contention();
        else return 2;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
