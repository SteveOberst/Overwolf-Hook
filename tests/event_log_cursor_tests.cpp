#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <owh/ipc/event_log.hpp>
#include <owh/platform/identity.hpp>
#include <stdexcept>
#include <string>
using namespace overwolf_hook;
using namespace std::chrono_literals;
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
namespace {
std::string unique_name() { return "events-" + std::to_string(platform::random_id()); }
struct Log {
    std::string name = unique_name();
    std::unique_ptr<ipc::EventLog> channel;
    Handle wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    explicit Log(uint32_t slots = 4) : channel(ipc::EventLog::create(name.c_str(), slots)) {}
    auto cursor(uint32_t filter = 0, uint32_t producer_filter = 0) {
        auto [status, value] = ipc::EventCursor::open(
            name.c_str(), channel->position(), filter, GetCurrentProcess(),
            reinterpret_cast<uint64_t>(wake.get()), reinterpret_cast<uint64_t>(closed.get()), producer_filter);
        REQUIRE(status.status == OWH_OK);
        return std::move(value);
    }
    void send(uint32_t value, uint32_t source = 23, uint64_t loss = 0, bool notify = true,
              uint32_t producer = 0) {
        core::CapturedEvent event{};
        event.captured_qpc = 99;
        event.source_channel = source;
        event.producer_process_id = producer;
        event.raw_size = sizeof(value);
        std::memcpy(event.raw, &value, sizeof(value));
        for (unsigned retry = 0;; ++retry) {
            const auto status = channel->publish(event, loss);
            if (status.status == OWH_OK)
                break;
            REQUIRE(status.status == OWH_BUSY && retry < 100);
            Sleep(1);
        }
        if (notify)
            REQUIRE(SetEvent(wake.get()));
    }
};
owh_event read(ipc::EventCursor &cursor, uint32_t expected) {
    owh_event out{};
    REQUIRE(cursor.receive(1000, nullptr, out).status == OWH_OK);
    uint32_t value = 0;
    std::memcpy(&value, out.raw, sizeof(value));
    REQUIRE(value == expected && out.raw_size == 4);
    return out;
}
void roundtrip() {
    Log log;
    auto first = log.cursor();
    log.send(1);
    REQUIRE(read(*first, 1).sequence == 1);
    // New subscription positions exclude already-published records.
    auto second = log.cursor();
    owh_event out{};
    REQUIRE(second->poll(out).status == OWH_NO_DATA);
    log.send(2);
    REQUIRE(read(*first, 2).sequence == 2);
    REQUIRE(read(*second, 2).sequence == 2);
    REQUIRE(log.channel->region->size() <= ipc::event_mapping_limit);
    REQUIRE(log.channel->header().slots <= 64);
    auto selected = log.cursor(23, 101);
    log.send(3, 23, 0, true, 202);
    log.send(4, 23, 0, true, 101);
    const auto filtered = read(*selected, 4);
    REQUIRE(filtered.producer_process_id == 101);
}
void overflow() {
    Log log(2);
    auto cursor = log.cursor(23);
    log.send(1, 7);
    log.send(2, 7);
    log.send(3, 7, 2);
    log.send(4, 23, 5);
    auto event = read(*cursor, 4);
    REQUIRE(event.sequence == 4 && event.lost_before == 7); // 2 overwritten + 5 global drops
    log.send(5, 7, 6);
    owh_event out{};
    REQUIRE(cursor->poll(out).status == OWH_NO_DATA);
    log.send(6, 23, 7);
    REQUIRE(read(*cursor, 6).lost_before == 2); // carries loss across filters
}
void notifications() {
    Log log;
    auto cursor = log.cursor();
    auto waiting = std::async(std::launch::async, [&] { return read(*cursor, 8); });
    REQUIRE(waiting.wait_for(20ms) == std::future_status::timeout);
    log.send(8, 23, 0, false); // publication survived a missed notification
    REQUIRE(waiting.wait_for(500ms) == std::future_status::ready);
    REQUIRE(waiting.get().sequence == 1);
    owh_event event{};
    REQUIRE(cursor->receive(10, nullptr, event).status == OWH_TIMEOUT);
    Handle cancel{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    auto cancelled = std::async(std::launch::async,
                                [&] { return cursor->receive(INFINITE, cancel.get(), event); });
    SetEvent(cancel.get());
    REQUIRE(cancelled.get().status == OWH_CANCELLED);
    // Cancellation belongs to this wait; the cursor itself remains usable.
    log.send(9);
    REQUIRE(read(*cursor, 9).sequence == 2);
    SetEvent(log.closed.get());
    REQUIRE(cursor->poll(event).status == OWH_CLOSED);
}
void abandoned() {
    Log log;
    auto cursor = log.cursor();
    std::thread client([&] { log.channel->header().mutex.lock(); });
    client.join(); // Models a reader disappearing while it owns the shared lock.
    owh_event event{};
    // Boost's default Windows backend is a spin mutex: it has no owner-death
    // recovery. Preserve bounded pressure/timeout semantics rather than claiming
    // to recover or force-unlock a possibly incomplete shared record.
    REQUIRE(cursor->poll(event).status == OWH_BUSY);
    const auto start = GetTickCount64();
    REQUIRE(cursor->receive(30, nullptr, event).status == OWH_TIMEOUT);
    REQUIRE(GetTickCount64() - start < 1000);
}
void corrupt() {
    Log log;
    auto cursor = log.cursor();
    log.send(1);
    {
        ipc::SharedLock lock(log.channel->header().mutex, 100);
        REQUIRE(lock.status() == ipc::SharedLockStatus::locked);
        ipc::EventLogSlot bad{99, 0};
        std::memcpy(log.channel->slot(1), &bad, sizeof(bad));
    }
    owh_event event{};
    REQUIRE(cursor->poll(event).status == OWH_CORRUPT);
    auto [status, ignored] =
        ipc::EventCursor::open(log.name.c_str(), {1, 0}, 0, GetCurrentProcess(), 0, 0);
    REQUIRE(status.status != OWH_OK && !ignored);
}
struct Descriptor {
    uint64_t wake, closed;
};
std::string control_name(const std::string &name) { return "Local\\" + name + ".control"; }
std::wstring event_name(const std::string &name, const wchar_t *suffix) {
    return L"Local\\" + std::wstring(name.begin(), name.end()) + suffix;
}
int producer(const std::string &name) {
    auto control = ipc::SharedRegion::open(control_name(name).c_str(), sizeof(Descriptor));
    Log log;
    log.channel.reset();
    log.name = name;
    log.channel = ipc::EventLog::create(name.c_str(), 4);
    *static_cast<Descriptor *>(control->data()) = {reinterpret_cast<uint64_t>(log.wake.get()),
                                                   reinterpret_cast<uint64_t>(log.closed.get())};
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name(name, L".ready").c_str()));
    Handle stop(OpenEventW(SYNCHRONIZE, FALSE, event_name(name, L".stop").c_str()));
    REQUIRE(ready && stop);
    log.send(101);
    SetEvent(ready.get());
    REQUIRE(WaitForSingleObject(stop.get(), 5000) == WAIT_OBJECT_0);
    SetEvent(log.closed.get());
    return 0;
}
void process(bool crash) {
    const auto name = unique_name();
    auto control = ipc::SharedRegion::create(sizeof(Descriptor), control_name(name).c_str());
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, event_name(name, L".ready").c_str()));
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, event_name(name, L".stop").c_str()));
    wchar_t exe[32768]{};
    REQUIRE(GetModuleFileNameW(nullptr, exe, 32768));
    auto command =
        L"\"" + std::wstring(exe) + L"\" --producer " + std::wstring(name.begin(), name.end());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &info));
    Handle child(info.hProcess), thread(info.hThread);
    REQUIRE(WaitForSingleObject(ready.get(), 5000) == WAIT_OBJECT_0);
    auto d = *static_cast<Descriptor *>(control->data());
    auto [status, cursor] =
        ipc::EventCursor::open(name.c_str(), {1, 0}, 0, child.get(), d.wake, d.closed);
    REQUIRE(status.status == OWH_OK);
    REQUIRE(read(*cursor, 101).sequence == 1);
    auto waiting = std::async(std::launch::async, [&] {
        owh_event out{};
        return cursor->receive(INFINITE, nullptr, out).status;
    });
    REQUIRE(waiting.wait_for(20ms) == std::future_status::timeout);
    if (crash)
        REQUIRE(TerminateProcess(child.get(), 23));
    else
        SetEvent(stop.get());
    REQUIRE(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0);
    REQUIRE(waiting.wait_for(1s) == std::future_status::ready);
    const auto ended = waiting.get();
    REQUIRE(crash ? ended == OWH_AGENT_LOST : (ended == OWH_CLOSED || ended == OWH_AGENT_LOST));
}
} // namespace
int main(int argc, char **argv) {
    try {
        REQUIRE(argc >= 2);
        const std::string test = argv[1];
        if (test == "--producer") {
            REQUIRE(argc == 3);
            return producer(argv[2]);
        }
        if (test == "roundtrip")
            roundtrip();
        else if (test == "overflow")
            overflow();
        else if (test == "notifications")
            notifications();
        else if (test == "corrupt")
            corrupt();
        else if (test == "abandoned")
            abandoned();
        else if (test == "process")
            process(false);
        else if (test == "crash")
            process(true);
        else
            throw std::runtime_error("unknown case");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
