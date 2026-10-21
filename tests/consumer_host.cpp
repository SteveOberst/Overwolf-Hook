#include <overwolf_hook/probe.h>
#include "capture/overwolf/profile.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

namespace {
namespace profile = overwolf_hook::overwolf;
template<typename T> void put(void* base, size_t offset, T value) {
    std::memcpy(static_cast<char*>(base) + offset, &value, sizeof(value));
}
uint64_t created(HANDLE process = GetCurrentProcess()) {
    FILETIME a{}, b{}, c{}, d{};
    REQUIRE(GetProcessTimes(process, &a, &b, &c, &d));
    return (uint64_t{a.dwHighDateTime} << 32) | a.dwLowDateTime;
}
struct Fixture {
    HMODULE consumer = nullptr;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, nullptr);
    HANDLE retired = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    alignas(8) std::array<unsigned char, 80> reader{};
    alignas(8) std::array<unsigned char, 96> service{}, mapping{}, synchronization{};
    std::array<uint64_t, 30> timestamps{};
    std::vector<unsigned char> memory = std::vector<unsigned char>(4 + 30 * 8 + 30 * profile::raw_event_bytes);
    std::vector<unsigned char> output = std::vector<unsigned char>(30 * profile::raw_event_bytes);
    char name[32] = "ow_events_900001";
    profile::EventRange range{};
    owh_reader* received = nullptr;
    ~Fixture() {
        owh_probe_stop(nullptr);
        owh_reader_close(received);
        if (mutex) CloseHandle(mutex);
        if (retired) CloseHandle(retired);
        // Consumer/capture are deliberately pinned by explicit probe start.
        if (consumer) FreeLibrary(consumer);
    }
    void populate() {
        REQUIRE(mutex && retired);
        put(service.data(), 8, static_cast<void*>(name));
        put(service.data(), 0x18, size_t{std::strlen(name)});
        put(service.data(), 0x20, size_t{31});
        put(service.data(), 0x30, static_cast<void*>(mapping.data()));
        put(service.data(), 0x40, static_cast<void*>(synchronization.data()));
        put(mapping.data(), 0x38, memory.data());
        put(synchronization.data(), 8, mutex);
        put(synchronization.data(), 0x10, retired);
        put(reader.data(), 0xc, uint32_t{30});
        put(reader.data(), 0x20, timestamps.data());
        put(reader.data(), 0x28, timestamps.data() + timestamps.size());
        put(reader.data(), 0x30, timestamps.data() + timestamps.size());
        put(reader.data(), 0x38, static_cast<void*>(service.data()));
        put(memory.data(), 0, uint32_t{30});
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        put(memory.data(), 4, now.QuadPart);
        put(memory.data(), 12, now.QuadPart); // Equal times: both records must survive.
        for (size_t i = 0; i < 2; ++i) {
            auto* raw = memory.data() + 4 + 30 * 8 + i * profile::raw_event_bytes;
            const char event[] = "fixture.synthetic";
            std::memcpy(raw, event, sizeof(event));
            raw[255] = static_cast<unsigned char>(0xf0 + i);
            raw[256] = 0; raw[257] = 255; // Arbitrary non-text bytes survive.
        }
        range = {reinterpret_cast<uintptr_t>(output.data()), reinterpret_cast<uintptr_t>(output.data()),
            reinterpret_cast<uintptr_t>(output.data() + output.size())};
    }
};
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--owner") { Sleep(INFINITE); return 0; }
        if (argc == 4 && std::string(argv[1]) == "--serve") {
            // Owned remote-controller fixture only. Its target filename is set
            // by CMake; never launches or opens a real Overwolf/game process.
            Fixture f;
            f.consumer = LoadLibraryExA(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            REQUIRE(f.consumer);
            auto read = reinterpret_cast<uintptr_t(__fastcall*)(void*, profile::EventRange*, uintptr_t, uintptr_t)>(
                reinterpret_cast<unsigned char*>(f.consumer) + profile::reader_rva);
            { std::ofstream ready(argv[3]); ready << "owned fixture ready\n"; REQUIRE(ready.good()); }
            for (;;) {
                f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
                REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
                Sleep(50);
            }
        }
        REQUIRE(argc == 2);
        Fixture f;
        f.consumer = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        REQUIRE(f.consumer);
        auto read = reinterpret_cast<uintptr_t(__fastcall*)(void*, profile::EventRange*, uintptr_t, uintptr_t)>(
            reinterpret_cast<unsigned char*>(f.consumer) + profile::reader_rva);
        // A reviewed layout fixture around the actual consumer body, not a
        // reimplementation of that body's copying or timestamp selection.
        f.populate();
        owh_probe_options options{sizeof(options), OWH_ABI_VERSION, GetCurrentProcessId(), 900001, created(), {}};
        const auto channel = "consumer-host-" + std::to_string(GetCurrentProcessId());
        std::memcpy(options.channel, channel.c_str(), channel.size() + 1);
        REQUIRE(owh_probe_start(&options) == OWH_OK);
        REQUIRE(owh_probe_start(&options) == OWH_ALREADY_EXISTS);
        REQUIRE(owh_reader_open(1, options.channel, OWH_NEW_ONLY, &f.received).status == OWH_OK);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end - f.range.begin == 2 * profile::raw_event_bytes);
        for (unsigned char i = 0; i < 2; ++i) {
            owh_probe_frame frame{}; owh_record record{};
            REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_OK);
            REQUIRE(record.sequence == i + 1u && record.size == sizeof(frame));
            REQUIRE(frame.source_channel == 900001 && frame.raw_size == profile::raw_event_bytes);
            REQUIRE(frame.raw[255] == 0xf0 + i && frame.raw[256] == 0 && frame.raw[257] == 255);
            REQUIRE(std::memcmp(frame.raw, f.output.data() + i * profile::raw_event_bytes, profile::raw_event_bytes) == 0);
        }
        // The original local watermark must still suppress repeat source records.
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end == f.range.begin);
        f.name[std::strlen(f.name) - 1] = '2';
        put(f.reader.data(), 0x18, uint64_t{0});
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end - f.range.begin == 2 * profile::raw_event_bytes);
        owh_probe_statistics stats{}; REQUIRE(owh_probe_query(&stats) == OWH_OK);
        REQUIRE(stats.calls == 3 && stats.forwarded == 2 && stats.filtered == 1 && stats.invalid == 0 && stats.hook_enabled == 1);
        REQUIRE(owh_probe_stop(nullptr) == OWH_OK);
        REQUIRE(owh_probe_query(&stats) == OWH_OK && stats.hook_enabled == 0 && stats.active_calls == 0);
        REQUIRE(std::memcmp(reinterpret_cast<const unsigned char*>(f.consumer) + profile::reader_rva,
            profile::reader_entry.data(), profile::reader_entry.size()) == 0);
        owh_probe_frame frame{}; owh_record record{};
        REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_CLOSED);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end == f.range.begin);
        owh_reader_close(f.received); f.received = nullptr;
        // Re-enable the same retained trampoline without creating another hook
        // or accidentally sharing a previous recording channel.
        f.name[std::strlen(f.name) - 1] = '1';
        f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
        REQUIRE(owh_probe_start(&options) == OWH_OK);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        // Exercise the recorder's start/stop boundaries: data produced before
        // reader open remains readable through that reader after hook shutdown.
        REQUIRE(owh_reader_open(1, options.channel, OWH_RETAINED, &f.received).status == OWH_OK);
        REQUIRE(owh_probe_stop(nullptr) == OWH_OK);
        for (int i = 0; i < 2; ++i)
            REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_OK);
        REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_CLOSED);
        REQUIRE(owh_probe_query(&stats) == OWH_OK && stats.forwarded == 2 && stats.active_calls == 0);
        owh_reader_close(f.received); f.received = nullptr;
        // A single wildcard session must preserve distinct source identities,
        // including newly encountered channels, and reject non-event services.
        options.source_channel = OWH_ALL_CHANNELS;
        REQUIRE(owh_probe_start(&options) == OWH_OK);
        REQUIRE(owh_reader_open(1, options.channel, OWH_RETAINED, &f.received).status == OWH_OK);
        const uint32_t channels[]{900001, 900002, UINT32_MAX};
        uint64_t sequence = 0;
        for (const auto channel_id : channels) {
            const auto service_name = "ow_events_" + std::to_string(channel_id);
            std::memcpy(f.name, service_name.c_str(), service_name.size() + 1);
            f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
            REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
            for (int i = 0; i < 2; ++i) {
                REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_OK);
                REQUIRE(frame.source_channel == channel_id && record.sequence == ++sequence);
                REQUIRE(std::memcmp(frame.raw, f.output.data() + i * profile::raw_event_bytes,
                    profile::raw_event_bytes) == 0);
            }
        }
        std::memcpy(f.name, "ow_info_900001", sizeof("ow_info_900001"));
        f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_NO_DATA);
        REQUIRE(owh_probe_query(&stats) == OWH_OK && stats.forwarded == 6 && stats.filtered == 1);
        REQUIRE(owh_probe_stop(nullptr) == OWH_OK);
        REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_CLOSED);
        owh_reader_close(f.received); f.received = nullptr;
        // The controller can disappear without calling stop. Use only our owned
        // child as the owner; the OS watcher must restore the real entry bytes.
        struct Child {
            HANDLE process = nullptr, thread = nullptr;
            ~Child() {
                if (process) { TerminateProcess(process, 0); CloseHandle(process); }
                if (thread) CloseHandle(thread);
            }
        } child;
        wchar_t self[32768]; REQUIRE(GetModuleFileNameW(nullptr, self, 32768));
        std::wstring command = L"\"" + std::wstring(self) + L"\" --owner";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        REQUIRE(CreateProcessW(self, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process));
        child.process = process.hProcess; child.thread = process.hThread;
        options.owner_process_id = process.dwProcessId; options.owner_started = created(child.process);
        REQUIRE(owh_probe_start(&options) == OWH_OK);
        REQUIRE(owh_reader_open(1, options.channel, OWH_RETAINED, &f.received).status == OWH_OK);
        REQUIRE(TerminateProcess(child.process, 0));
        const auto timeout = GetTickCount64() + 5000;
        do {
            REQUIRE(owh_probe_query(&stats) == OWH_OK);
            if (stats.status == OWH_CLOSED && !stats.hook_enabled && !stats.active_calls) break;
            Sleep(1);
        } while (GetTickCount64() < timeout);
        REQUIRE(stats.status == OWH_CLOSED && !stats.hook_enabled && !stats.active_calls);
        REQUIRE(owh_reader_next(f.received, &frame, sizeof(frame), &record).status == OWH_CLOSED);
        REQUIRE(std::memcmp(reinterpret_cast<const unsigned char*>(f.consumer) + profile::reader_rva,
            profile::reader_entry.data(), profile::reader_entry.size()) == 0);
        std::cout << "PASS actual consumer trampoline, channel selection, multiplexing, exact raw bytes, watermark, stop, restart, drain\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << " (Win32 " << GetLastError() << ")\n"; return 1;
    }
}
