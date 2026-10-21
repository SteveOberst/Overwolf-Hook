#include <event_tap/probe.h>
#include "capture/overwolf/profile.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

namespace {
namespace profile = event_tap::overwolf;
template<typename T> void put(void* base, size_t offset, T value) {
    std::memcpy(static_cast<char*>(base) + offset, &value, sizeof(value));
}
uint64_t created() {
    FILETIME a{}, b{}, c{}, d{};
    REQUIRE(GetProcessTimes(GetCurrentProcess(), &a, &b, &c, &d));
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
    oet_reader* received = nullptr;
    ~Fixture() {
        oet_probe_stop(nullptr);
        oet_reader_close(received);
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
        REQUIRE(argc == 2);
        Fixture f;
        f.consumer = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        REQUIRE(f.consumer);
        auto read = reinterpret_cast<uintptr_t(__fastcall*)(void*, profile::EventRange*, uintptr_t, uintptr_t)>(
            reinterpret_cast<unsigned char*>(f.consumer) + profile::reader_rva);
        // A reviewed layout fixture around the actual consumer body, not a
        // reimplementation of that body's copying or timestamp selection.
        f.populate();
        oet_probe_options options{sizeof(options), OET_ABI_VERSION, GetCurrentProcessId(), 900001, created(), {}};
        const auto channel = "consumer-host-" + std::to_string(GetCurrentProcessId());
        std::memcpy(options.channel, channel.c_str(), channel.size() + 1);
        REQUIRE(oet_probe_start(&options) == OET_OK);
        REQUIRE(oet_probe_start(&options) == OET_ALREADY_EXISTS);
        REQUIRE(oet_reader_open(1, options.channel, OET_NEW_ONLY, &f.received).status == OET_OK);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end - f.range.begin == 2 * profile::raw_event_bytes);
        for (unsigned char i = 0; i < 2; ++i) {
            oet_probe_frame frame{}; oet_record record{};
            REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_OK);
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
        oet_probe_statistics stats{}; REQUIRE(oet_probe_query(&stats) == OET_OK);
        REQUIRE(stats.calls == 3 && stats.forwarded == 2 && stats.filtered == 1 && stats.invalid == 0 && stats.hook_enabled == 1);
        REQUIRE(oet_probe_stop(nullptr) == OET_OK);
        REQUIRE(oet_probe_query(&stats) == OET_OK && stats.hook_enabled == 0 && stats.active_calls == 0);
        REQUIRE(std::memcmp(reinterpret_cast<const unsigned char*>(f.consumer) + profile::reader_rva,
            profile::reader_entry.data(), profile::reader_entry.size()) == 0);
        oet_probe_frame frame{}; oet_record record{};
        REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_CLOSED);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(f.range.end == f.range.begin);
        oet_reader_close(f.received); f.received = nullptr;
        // Re-enable the same retained trampoline without creating another hook
        // or accidentally sharing a previous recording channel.
        f.name[std::strlen(f.name) - 1] = '1';
        f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
        REQUIRE(oet_probe_start(&options) == OET_OK);
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        // Exercise the recorder's start/stop boundaries: data produced before
        // reader open remains readable through that reader after hook shutdown.
        REQUIRE(oet_reader_open(1, options.channel, OET_RETAINED, &f.received).status == OET_OK);
        REQUIRE(oet_probe_stop(nullptr) == OET_OK);
        for (int i = 0; i < 2; ++i)
            REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_OK);
        REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_CLOSED);
        REQUIRE(oet_probe_query(&stats) == OET_OK && stats.forwarded == 2 && stats.active_calls == 0);
        oet_reader_close(f.received); f.received = nullptr;
        // A single wildcard session must preserve distinct source identities,
        // including newly encountered channels, and reject non-event services.
        options.source_channel = OET_ALL_CHANNELS;
        REQUIRE(oet_probe_start(&options) == OET_OK);
        REQUIRE(oet_reader_open(1, options.channel, OET_RETAINED, &f.received).status == OET_OK);
        const uint32_t channels[]{900001, 900002, UINT32_MAX};
        uint64_t sequence = 0;
        for (const auto channel_id : channels) {
            const auto service_name = "ow_events_" + std::to_string(channel_id);
            std::memcpy(f.name, service_name.c_str(), service_name.size() + 1);
            f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
            REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
            for (int i = 0; i < 2; ++i) {
                REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_OK);
                REQUIRE(frame.source_channel == channel_id && record.sequence == ++sequence);
                REQUIRE(std::memcmp(frame.raw, f.output.data() + i * profile::raw_event_bytes,
                    profile::raw_event_bytes) == 0);
            }
        }
        std::memcpy(f.name, "ow_info_900001", sizeof("ow_info_900001"));
        f.populate(); put(f.reader.data(), 0x18, uint64_t{0});
        REQUIRE(read(f.reader.data(), &f.range, 2000, 0) == 1);
        REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_NO_DATA);
        REQUIRE(oet_probe_query(&stats) == OET_OK && stats.forwarded == 6 && stats.filtered == 1);
        REQUIRE(oet_probe_stop(nullptr) == OET_OK);
        REQUIRE(oet_reader_next(f.received, &frame, sizeof(frame), &record).status == OET_CLOSED);
        std::cout << "PASS actual consumer trampoline, channel selection, multiplexing, exact raw bytes, watermark, stop, restart, drain\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << " (Win32 " << GetLastError() << ")\n"; return 1;
    }
}
