#include <overwolf_hook/discovery.h>
#include <overwolf_hook/state.h>
#include "capture_control.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {
HANDLE requested = nullptr;
BOOL WINAPI interrupt(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT) return FALSE;
    SetEvent(requested); return TRUE;
}
struct Signal {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Signal() {
        if (!event) throw std::runtime_error("Cannot create stop signal");
        requested = event;
        if (!SetConsoleCtrlHandler(interrupt, TRUE)) { CloseHandle(event); throw std::runtime_error("Cannot register console handler"); }
    }
    ~Signal() { SetConsoleCtrlHandler(interrupt, FALSE); CloseHandle(event); }
};
struct Channels { owh_channels* value = nullptr; ~Channels() { owh_channels_close(value); } };
struct State { owh_state* value = nullptr; ~State() { owh_state_close(value); } };
std::string hex(const void* data, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string output; output.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) { output += digits[bytes[i] >> 4]; output += digits[bytes[i] & 15]; }
    return output;
}
std::string read(uint32_t channel) {
    State state;
    const auto result = owh_state_read(OWH_ABI_VERSION, channel, 100, &state.value);
    std::ostringstream out;
    out << "\"status\":" << result.status << ",\"native_error\":" << result.native_error;
    if (result.status != OWH_OK) return out.str();
    out << ",\"fields\":[";
    for (uint32_t i = 0; i < owh_state_count(state.value); ++i) {
        owh_state_field field{};
        if (owh_state_at(state.value, i, &field).status != OWH_OK) throw std::runtime_error("Invalid owned state field");
        if (i) out << ',';
        // Names as hex also retain non-UTF-8 bytes without invalid JSON or escaping ambiguity.
        out << "{\"category_hex\":\"" << hex(field.category, std::strlen(field.category))
            << "\",\"key_hex\":\"" << hex(field.key, std::strlen(field.key))
            << "\",\"producer_ticks\":\"" << field.producer_ticks
            << "\",\"reserved_bytes\":" << field.reserved_bytes
            << ",\"value_hex\":\"" << hex(field.bytes, field.size) << "\"}";
    }
    out << ']'; return out.str();
}
uint64_t utc() {
    FILETIME now{}; GetSystemTimePreciseAsFileTime(&now);
    return (uint64_t{now.dwHighDateTime} << 32) | now.dwLowDateTime;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        using namespace overwolf_hook::controller;
        if (argc == 2 && std::wstring_view(argv[1]) == L"list") {
            Channels channels;
            const auto result = owh_channels_discover(OWH_ABI_VERSION, &channels.value);
            if (result.status != OWH_OK) throw std::runtime_error("Discovery failed: " + std::to_string(result.status) + "/" + std::to_string(result.native_error));
            std::cout << "{\"channels\":[";
            for (uint32_t i = 0; i < owh_channels_count(channels.value); ++i) {
                owh_source_channel channel{};
                if (owh_channels_at(channels.value, i, &channel).status != OWH_OK) throw std::runtime_error("Invalid channel");
                if (i) std::cout << ',';
                std::cout << "{\"source_channel\":" << channel.source_channel << ",\"kinds\":" << channel.kinds << '}';
            }
            std::cout << "]}\n"; return 0;
        }
        if (argc == 3 && std::wstring_view(argv[1]) == L"read") {
            const auto id = number(argv[2], "Expected a nonzero source channel");
            if (!id || id > UINT32_MAX) throw std::runtime_error("Expected a nonzero uint32 source channel");
            const auto value = read(static_cast<uint32_t>(id));
            std::cout << "{\"source_channel\":" << id << ',' << value << "}\n";
            return value.starts_with("\"status\":0,") ? 0 : 1;
        }
        if ((argc != 4 && argc != 6) || std::wstring_view(argv[1]) != L"watch") {
            std::cerr << "Usage: owh_info list | read <source-channel> | watch <seconds|0=until-stop> <new JSONL file> [--stop-file <path>]\n";
            return 2;
        }
        std::filesystem::path stop_file;
        if (argc == 6) {
            if (std::wstring_view(argv[4]) != L"--stop-file") throw std::runtime_error("Expected --stop-file");
            stop_file = std::filesystem::absolute(argv[5]);
            if (std::filesystem::exists(stop_file)) throw std::runtime_error("Stop file already exists");
        }
        Signal signal;
        CaptureControl control(number(argv[2], "Expected duration or 0"), signal.event, nullptr, stop_file);
        // C++23 noreplace is not required: use CREATE_NEW for atomic refusal to overwrite.
        HANDLE file = CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Output must be a new writable file");
        struct File { HANDLE value; ~File() { CloseHandle(value); } } output{file};
        auto write = [&](const std::string& line) {
            DWORD written = 0;
            if (line.size() > MAXDWORD || !WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size())
                throw std::runtime_error("State recording write failed");
        };
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        write("{\"format\":\"owh-infodb-jsonl-1\",\"qpc_frequency\":\"" + std::to_string(frequency.QuadPart) + "\",\"started_filetime_utc\":\"" + std::to_string(utc()) + "\"}\n");
        std::map<uint32_t, std::string> previous;
        bool discovery_failed = false;
        std::cout << "info recording started\n" << std::flush;
        while (control.reason() == StopReason::None) {
            Channels channels;
            const auto result = owh_channels_discover(OWH_ABI_VERSION, &channels.value);
            LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
            const auto time = "\"received_qpc\":\"" + std::to_string(now.QuadPart) + "\",\"filetime_utc\":\"" + std::to_string(utc()) + "\"";
            if (result.status != OWH_OK) {
                write("{" + time + ",\"discovery_status\":" + std::to_string(result.status) + ",\"native_error\":" + std::to_string(result.native_error) + "}\n");
                previous.clear(); discovery_failed = true;
            } else {
                if (discovery_failed) { write("{" + time + ",\"discovery_status\":0}\n"); discovery_failed = false; }
                std::map<uint32_t, std::string> current;
                for (uint32_t i = 0; i < owh_channels_count(channels.value); ++i) {
                    owh_source_channel channel{};
                    if (owh_channels_at(channels.value, i, &channel).status != OWH_OK) throw std::runtime_error("Invalid discovered channel");
                    if (!(channel.kinds & OWH_CHANNEL_INFO)) continue;
                    auto value = read(channel.source_channel);
                    const auto found = previous.find(channel.source_channel);
                    if (found == previous.end() || found->second != value)
                        write("{" + time + ",\"source_channel\":" + std::to_string(channel.source_channel) + ',' + value + "}\n");
                    current.emplace(channel.source_channel, std::move(value));
                }
                for (const auto& [id, value] : previous) {
                    (void)value;
                    if (!current.contains(id)) write("{" + time + ",\"source_channel\":" + std::to_string(id) + ",\"removed\":true}\n");
                }
                previous = std::move(current);
            }
            Sleep(250); // Polling: intermediate provider changes may collapse.
        }
        write("{\"stopped_filetime_utc\":\"" + std::to_string(utc()) + "\"}\n");
        if (!FlushFileBuffers(file)) throw std::runtime_error("State recording flush failed");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
