#include <overwolf_hook/bridge.h>
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <future>
#include <chrono>
#include <thread>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Writer {
    owh_writer* value = nullptr;
    ~Writer() { if (value) owh_writer_close(value); }
};
struct Reader {
    owh_reader* value = nullptr;
    ~Reader() { owh_reader_close(value); }
};
std::string channel_name() {
    return "test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
}
void create(Writer& writer, const std::string& name, uint32_t slots = 4, uint32_t bytes = 64) {
    REQUIRE(owh_writer_create(OWH_ABI_VERSION, name.c_str(), slots, bytes, &writer.value).status == OWH_OK);
}
void open(Reader& reader, const std::string& name, owh_start start = OWH_RETAINED) {
    REQUIRE(owh_reader_open(OWH_ABI_VERSION, name.c_str(), start, &reader.value).status == OWH_OK);
}
void publish(Writer& writer, uint32_t number) {
    REQUIRE(owh_writer_publish(writer.value, &number, sizeof(number)).status == OWH_OK);
}
owh_record read(Reader& reader, uint32_t expected) {
    owh_record record{};
    uint32_t value = 0;
    REQUIRE(owh_reader_next(reader.value, &value, sizeof(value), &record).status == OWH_OK);
    REQUIRE(record.size == sizeof(value));
    REQUIRE(value == expected);
    return record;
}
owh_status next_status(Reader& reader) {
    owh_record record{};
    uint32_t value;
    return owh_reader_next(reader.value, &value, sizeof(value), &record).status;
}

void roundtrip() {
    const auto name = channel_name(); Writer writer; Reader reader;
    create(writer, name); open(reader, name);
    REQUIRE(next_status(reader) == OWH_NO_DATA);
    const std::array<unsigned char, 7> source{0, 255, 1, 0, 128, 2, 3};
    REQUIRE(owh_writer_publish(writer.value, source.data(), 7).status == OWH_OK);
    std::array<unsigned char, 7> target{}; owh_record record{};
    REQUIRE(owh_reader_next(reader.value, target.data(), 7, &record).status == OWH_OK);
    REQUIRE(source == target && record.sequence == 1 && !record.lost_before);
    REQUIRE(next_status(reader) == OWH_NO_DATA);
    REQUIRE(owh_writer_publish(writer.value, nullptr, 0).status == OWH_OK);
    REQUIRE(owh_reader_next(reader.value, nullptr, 0, &record).status == OWH_OK);
    REQUIRE(record.sequence == 2 && record.size == 0);
}
void readers() {
    const auto name = channel_name(); Writer writer; Reader first, second;
    create(writer, name); open(first, name); open(second, name);
    publish(writer, 42); publish(writer, 43);
    REQUIRE(read(first, 42).sequence == 1); REQUIRE(read(first, 43).sequence == 2);
    REQUIRE(read(second, 42).sequence == 1); REQUIRE(read(second, 43).sequence == 2);
}
void notifications() {
    using namespace std::chrono_literals;
    const auto name = channel_name(); Writer writer; Reader fast, slow;
    create(writer, name); open(fast, name); open(slow, name);
    const auto receive = [](Reader& reader) {
        owh_record record{}; uint32_t value = 0;
        const auto status = owh_reader_receive(reader.value, &value, sizeof(value), 2000, &record).status;
        REQUIRE(status == OWH_OK); return value;
    };
    // Publication deliberately does not wait for concurrent readers. Retry BUSY
    // only in this test harness so contention is not mistaken for a lost signal.
    const auto send = [&](uint32_t value) {
        const auto until = std::chrono::steady_clock::now() + 1s;
        for (;;) {
            const auto result = owh_writer_publish(writer.value, &value, sizeof(value));
            if (result.status == OWH_OK) return;
            REQUIRE(result.status == OWH_BUSY && std::chrono::steady_clock::now() < until);
            std::this_thread::yield();
        }
    };
    auto first = std::async(std::launch::async, [&] { return receive(fast); });
    auto second = std::async(std::launch::async, [&] { return receive(slow); });
    REQUIRE(first.wait_for(30ms) == std::future_status::timeout);
    REQUIRE(second.wait_for(30ms) == std::future_status::timeout);
    send(42);
    REQUIRE(first.get() == 42 && second.get() == 42);
    // A fast reader drains and rearms while a peer has not even started waiting.
    // The peer's notification and independent cursor must remain intact.
    for (uint32_t i = 1; i <= 100; ++i) {
        send(i);
        REQUIRE(receive(fast) == i);
        auto pending = std::async(std::launch::async, [&] { return receive(fast); });
        REQUIRE(receive(slow) == i);
        send(i + 1000);
        REQUIRE(pending.get() == i + 1000 && receive(slow) == i + 1000);
    }
    owh_record record{}; uint32_t value;
    REQUIRE(owh_reader_receive(fast.value, &value, sizeof(value), 20, &record).status == OWH_TIMEOUT);
    auto blocked = std::async(std::launch::async, [&] {
        return owh_reader_receive(fast.value, &value, sizeof(value), OWH_WAIT_INFINITE, &record).status;
    });
    REQUIRE(blocked.wait_for(30ms) == std::future_status::timeout);
    REQUIRE(owh_reader_cancel(fast.value).status == OWH_OK);
    REQUIRE(blocked.wait_for(1s) == std::future_status::ready && blocked.get() == OWH_CANCELLED);
    REQUIRE(next_status(fast) == OWH_CANCELLED);
    auto closing = std::async(std::launch::async, [&] {
        return owh_reader_receive(slow.value, &value, sizeof(value), OWH_WAIT_INFINITE, &record).status;
    });
    REQUIRE(closing.wait_for(30ms) == std::future_status::timeout);
    REQUIRE(owh_writer_close(writer.value).status == OWH_OK); writer.value = nullptr;
    REQUIRE(closing.wait_for(1s) == std::future_status::ready && closing.get() == OWH_CLOSED);
}
void registrations() {
    const auto name = channel_name(); Writer writer;
    create(writer, name);
    // Repeated short-lived readers must release their slots, not exhaust the registry.
    for (int i = 0; i < 200; ++i) { Reader reader; open(reader, name); }
    std::array<Reader, 64> readers;
    for (auto& reader : readers) open(reader, name);
    Reader excess;
    REQUIRE(owh_reader_open(OWH_ABI_VERSION, name.c_str(), OWH_RETAINED, &excess.value).status == OWH_RESOURCE_EXHAUSTED);
    REQUIRE(!excess.value);
    publish(writer, 7);
    for (auto& reader : readers) REQUIRE(read(reader, 7).sequence == 1);
}
void boundary() {
    const auto name = channel_name(); Writer writer; Reader retained, fresh;
    create(writer, name); publish(writer, 1);
    open(retained, name); open(fresh, name, OWH_NEW_ONLY);
    read(retained, 1); REQUIRE(next_status(fresh) == OWH_NO_DATA);
    publish(writer, 2); REQUIRE(read(fresh, 2).sequence == 2);
}
void overflow() {
    const auto name = channel_name(); Writer writer; Reader slow, late;
    create(writer, name, 3); open(slow, name);
    for (uint32_t i = 1; i <= 8; ++i) publish(writer, i);
    const auto gap = read(slow, 6);
    REQUIRE(gap.sequence == 6 && gap.lost_before == 5);
    REQUIRE(!read(slow, 7).lost_before); read(slow, 8);
    open(late, name); REQUIRE(!read(late, 6).lost_before);
}
void buffer() {
    const auto name = channel_name(); Writer writer; Reader reader;
    create(writer, name); open(reader, name); publish(writer, 0x12345678);
    char byte; owh_record record{};
    REQUIRE(owh_reader_next(reader.value, &byte, 1, &record).status == OWH_BUFFER_TOO_SMALL);
    REQUIRE(record.size == 4 && record.sequence == 1);
    REQUIRE(read(reader, 0x12345678).sequence == 1);
    REQUIRE(owh_writer_publish(writer.value, &byte, 65).status == OWH_INVALID_ARGUMENT);
    REQUIRE(next_status(reader) == OWH_NO_DATA);
}
void invalid() {
    Writer writer; Reader reader; const auto name = channel_name();
    REQUIRE(owh_reader_open(OWH_ABI_VERSION, name.c_str(), OWH_RETAINED, &reader.value).status == OWH_NOT_FOUND);
    REQUIRE(!reader.value);
    REQUIRE(owh_writer_create(999, name.c_str(), 1, 8, &writer.value).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_create(1, "bad\\name", 1, 8, &writer.value).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_create(1, "", 1, 8, &writer.value).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_create(1, name.c_str(), 0, 8, &writer.value).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_create(1, name.c_str(), 4096, 65536, &writer.value).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_publish(nullptr, nullptr, 0).status == OWH_INVALID_ARGUMENT);
    REQUIRE(owh_writer_close(nullptr).status == OWH_OK);
    owh_record record{};
    REQUIRE(owh_reader_next(nullptr, nullptr, 0, &record).status == OWH_INVALID_ARGUMENT);
}
void lifecycle() {
    const auto name = channel_name(); Writer writer, duplicate; Reader reader;
    create(writer, name); open(reader, name); publish(writer, 7);
    REQUIRE(owh_writer_create(1, name.c_str(), 4, 64, &duplicate.value).status == OWH_ALREADY_EXISTS);
    REQUIRE(!duplicate.value);
    REQUIRE(owh_writer_close(writer.value).status == OWH_OK); writer.value = nullptr;
    read(reader, 7); REQUIRE(next_status(reader) == OWH_CLOSED);
    // Old readers deliberately keep the old mapping alive. Never reinitialize it.
    REQUIRE(owh_writer_create(1, name.c_str(), 4, 64, &duplicate.value).status == OWH_ALREADY_EXISTS);
    owh_reader_close(reader.value); reader.value = nullptr;
    create(writer, name); open(reader, name); publish(writer, 8);
    REQUIRE(read(reader, 8).sequence == 1);
}
void corrupt() {
    const auto name = channel_name(); Writer writer; Reader reader;
    create(writer, name); open(reader, name);
    const std::string mapping_name = "Local\\overwolf-hook." + name + ".data";
    Handle mapping{OpenFileMappingA(FILE_MAP_WRITE, FALSE, mapping_name.c_str())};
    REQUIRE(mapping.value);
    void* view = MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, 0); REQUIRE(view);
    // Single-threaded adversarial fixture: mutate the wire ABI version (offset 4).
    const uint32_t bad_version = 900;
    std::memcpy(static_cast<char*>(view) + 4, &bad_version, sizeof(bad_version));
    UnmapViewOfFile(view);
    REQUIRE(next_status(reader) == OWH_INCOMPATIBLE);
}

std::wstring event_name(const std::string& name, const wchar_t* suffix) {
    return L"Local\\owh-test." + std::wstring(name.begin(), name.end()) + suffix;
}
int producer(const std::string& name) {
    Writer writer; create(writer, name);
    Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name(name, L".ready").c_str())};
    Handle stop{OpenEventW(SYNCHRONIZE, FALSE, event_name(name, L".stop").c_str())};
    REQUIRE(ready.value && stop.value);
    publish(writer, 101); publish(writer, 202); REQUIRE(SetEvent(ready.value));
    REQUIRE(WaitForSingleObject(stop.value, 10000) == WAIT_OBJECT_0);
    REQUIRE(owh_writer_close(writer.value).status == OWH_OK); writer.value = nullptr;
    return 0;
}

void process_test(bool crash, bool blocking = false) {
    const auto name = channel_name();
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, event_name(name, L".ready").c_str())};
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, event_name(name, L".stop").c_str())};
    REQUIRE(ready.value && stop.value);
    Handle job{CreateJobObjectW(nullptr, nullptr)}; REQUIRE(job.value);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    REQUIRE(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    std::vector<wchar_t> path(32768);
    REQUIRE(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    std::wstring command = L"\"" + std::wstring(path.data()) + L"\" --producer " + std::wstring(name.begin(), name.end());
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    REQUIRE(CreateProcessW(path.data(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process));
    Handle child{process.hProcess}; Handle thread{process.hThread};
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 99);
        throw std::runtime_error("AssignProcessToJobObject failed");
    }
    REQUIRE(ResumeThread(thread.value) != static_cast<DWORD>(-1));
    REQUIRE(WaitForSingleObject(ready.value, 5000) == WAIT_OBJECT_0);
    Reader first, second; open(first, name); open(second, name);
    read(first, 101); read(second, 101);
    std::future<owh_status> waiter;
    if (blocking) {
        read(first, 202); read(second, 202);
        waiter = std::async(std::launch::async, [&] {
            owh_record record{}; uint32_t value;
            return owh_reader_receive(first.value, &value, sizeof(value), OWH_WAIT_INFINITE, &record).status;
        });
        REQUIRE(waiter.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    }
    if (crash) REQUIRE(TerminateProcess(child.value, 23));
    else REQUIRE(SetEvent(stop.value));
    REQUIRE(WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0);
    DWORD code = 0; REQUIRE(GetExitCodeProcess(child.value, &code));
    REQUIRE(code == (crash ? 23u : 0u));
    if (blocking) {
        REQUIRE(waiter.wait_for(std::chrono::seconds(1)) == std::future_status::ready && waiter.get() == OWH_CLOSED);
    } else {
        uint32_t value; owh_record record{};
        REQUIRE(owh_reader_receive(first.value, &value, sizeof(value), 1000, &record).status == OWH_OK && value == 202);
        REQUIRE(owh_reader_receive(second.value, &value, sizeof(value), 1000, &record).status == OWH_OK && value == 202);
        REQUIRE(owh_reader_receive(first.value, &value, sizeof(value), 1000, &record).status == OWH_CLOSED);
    }
    REQUIRE(next_status(first) == OWH_CLOSED && next_status(second) == OWH_CLOSED);
}
}

int main(int argc, char** argv) {
    try {
        REQUIRE(argc >= 2);
        const std::string test = argv[1];
        if (test == "--producer") { REQUIRE(argc == 3); return producer(argv[2]); }
        if (test == "roundtrip") roundtrip();
        else if (test == "readers") readers();
        else if (test == "notifications") notifications();
        else if (test == "registrations") registrations();
        else if (test == "boundary") boundary();
        else if (test == "overflow") overflow();
        else if (test == "buffer") buffer();
        else if (test == "invalid") invalid();
        else if (test == "lifecycle") lifecycle();
        else if (test == "corrupt") corrupt();
        else if (test == "process") process_test(false);
        else if (test == "crash") process_test(true);
        else if (test == "wait_process") process_test(false, true);
        else if (test == "wait_crash") process_test(true, true);
        else throw std::runtime_error("Unknown test");
        std::cout << "PASS " << test << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << " (Win32 " << GetLastError() << ")\n"; return 1;
    }
}
