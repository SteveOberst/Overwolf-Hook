#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/result.hpp>
#include <string>
#undef assert
#define assert(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #test);                   \
            ExitProcess(2);                                                                        \
        }                                                                                          \
    } while (false)
using namespace overwolf_hook;
using namespace overwolf_hook::ipc;
namespace overwolf_hook::ipc {
struct WireTestAccess {
    static void wake(PixelProducer &producer, BOOL(WINAPI *signal)(HANDLE)) {
        producer.signal_wake_ = signal;
    }
};
} // namespace overwolf_hook::ipc
namespace {
PixelConsumer *racing_consumer = nullptr;
std::unique_ptr<PixelRead> racing_read;
BOOL WINAPI claim_then_fail(HANDLE) {
    auto [result, read] = racing_consumer->try_take_ready();
    assert(result.status == OWH_OK);
    racing_read = std::move(read);
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
uint64_t started(HANDLE p) {
    FILETIME a{}, b{}, c{}, d{};
    assert(GetProcessTimes(p, &a, &b, &c, &d));
    return (uint64_t(a.dwHighDateTime) << 32) | a.dwLowDateTime;
}
auto queue(uint32_t policy = OWH_QUEUE_FIFO) {
    return PixelProducer::create({71, 1}, GetCurrentProcessId(), started(GetCurrentProcess()), 2, 2,
                                 3, policy, 1 << 20);
}
auto imported(PixelProducer &p) {
    auto d = p.descriptor();
    return PixelConsumer::import(d, GetCurrentProcess(), d.ref, 2, 2, 3, d.queue_policy, 1 << 20);
}
void wake_failure_after_claim() {
    auto [status, producer] = queue();
    assert(status.status == OWH_OK);
    auto [import_status, consumer] = imported(*producer);
    assert(import_status.status == OWH_OK);
    auto acquired = producer->try_acquire();
    assert(acquired.result.status == OWH_OK);
    std::fill(acquired.frame.pixels.begin(), acquired.frame.pixels.end(), std::byte{0x7b});
    racing_consumer = consumer.get();
    WireTestAccess::wake(*producer, claim_then_fail);
    const auto committed = producer->commit(acquired.frame);
    assert(committed.result.status == OWH_AGENT_LOST && racing_read);
    assert(producer->abort(acquired.frame).status != OWH_OK);
    assert(consumer->settle_for_close().status == OWH_BUSY);
    assert(racing_read->pixels()[0] == std::byte{0x7b});
    racing_read->complete(make_result(OWH_OK));
    racing_read.reset();
    racing_consumer = nullptr;
    uint32_t count = 0;
    auto done = producer->collect_completed(count);
    assert(count == 1 && done[0].published);
    assert(consumer->settle_for_close().status == OWH_OK);
}
void managed_admission() {
    auto [created, producer] = queue(OWH_QUEUE_LATEST_READY);
    assert(created.status == OWH_OK);
    auto [imported_status, consumer] = imported(*producer);
    assert(imported_status.status == OWH_OK);
    auto first = producer->prepare_submit();
    assert(first.result.status == OWH_OK && !first.displaced);
    std::fill(first.frame.pixels.begin(), first.frame.pixels.end(), std::byte{7});
    assert(producer->commit(first.frame).result.status == OWH_OK);
    auto [taken, held] = consumer->try_take_ready();
    assert(taken.status == OWH_OK);
    uint64_t previous = 0;
    for (unsigned i = 1; i <= 100; ++i) {
        auto next = producer->prepare_submit();
        assert(next.result.status == OWH_OK);
        if (previous) {
            assert(next.displaced && next.displaced->sequence == previous);
            assert(next.displaced->result.status == OWH_REPLACED);
        }
        std::fill(next.frame.pixels.begin(), next.frame.pixels.end(), std::byte(i));
        const auto committed = producer->commit(next.frame);
        assert(committed.result.status == OWH_OK);
        previous = committed.sequence;
        assert(held->pixels()[0] == std::byte{7}); // never overwrite an active reader
    }
    held->complete(make_result(OWH_OK));
    held.reset();
    auto [latest_status, latest] = consumer->try_take_ready();
    assert(latest_status.status == OWH_OK && latest->sequence() == previous);
    assert(latest->pixels()[0] == std::byte{100});
    latest->complete(make_result(OWH_OK));
    latest.reset();
    // Reusing a completion returns its outcome before the slot is overwritten.
    auto reclaimed = producer->prepare_submit();
    assert(reclaimed.result.status == OWH_OK && reclaimed.displaced && reclaimed.displaced->published);
    assert(producer->prepare_submit().result.status == OWH_INVALID_ARGUMENT);
    assert(producer->abort(reclaimed.frame).status == OWH_OK);

    auto [fifo_status, fifo] = queue();
    assert(fifo_status.status == OWH_OK);
    auto [fc, reader] = imported(*fifo);
    assert(fc.status == OWH_OK);
    for (unsigned i = 1; i <= 3; ++i) {
        auto next = fifo->prepare_submit();
        assert(next.result.status == OWH_OK && !next.displaced);
        next.frame.pixels[0] = std::byte(i);
        assert(fifo->commit(next.frame).result.status == OWH_OK);
    }
    assert(fifo->prepare_submit().result.status == OWH_BUSY);
    for (unsigned i = 1; i <= 3; ++i) {
        auto [status, read] = reader->try_take_ready();
        assert(status.status == OWH_OK && read->pixels()[0] == std::byte(i));
        read->complete(make_result(OWH_OK));
    }
}
void accumulated_damage(uint32_t policy) {
    auto [created, producer] = PixelProducer::create({72, 1}, GetCurrentProcessId(),
        started(GetCurrentProcess()), 8, 8, 3, policy, 1 << 20);
    assert(created.status == OWH_OK);
    auto descriptor = producer->descriptor();
    auto [opened, consumer] = PixelConsumer::import(descriptor, GetCurrentProcess(),
        descriptor.ref, 8, 8, 3, policy, 1 << 20);
    assert(opened.status == OWH_OK);
    std::array<std::byte, 256> image{}, retained{};
    const auto submit = [&](const core::PixelRegion *damage) {
        auto frame = producer->prepare_submit();
        assert(frame.result.status == OWH_OK);
        std::copy(image.begin(), image.end(), frame.frame.pixels.begin());
        assert(producer->commit(frame.frame, damage).result.status == OWH_OK);
    };
    const auto take = [&] {
        auto [status, read] = consumer->try_take_ready();
        assert(status.status == OWH_OK);
        return std::move(read);
    };
    const auto apply = [&](PixelRead &read) {
        const auto r = read.damage();
        assert(core::valid_region(r, 8, 8));
        for (uint32_t y = r.y; y < r.y + r.height; ++y)
            std::copy_n(read.pixels().begin() + y * 32 + r.x * 4, r.width * 4,
                        retained.begin() + y * 32 + r.x * 4);
        assert(std::equal(retained.begin(), retained.end(), read.pixels().begin()));
        read.complete(make_result(OWH_OK));
    };
    const auto is = [](const PixelRead &read, core::PixelRegion expected) {
        const auto r = read.damage();
        assert(r.x == expected.x && r.y == expected.y &&
               r.width == expected.width && r.height == expected.height);
    };
    const core::PixelRegion a{1, 1, 1, 1}, b{5, 5, 1, 1};
    image.fill(std::byte{0x5a});
    submit(&a);
    auto first = take();
    is(*first, {0, 0, 8, 8}); // First publication always establishes every pixel.
    apply(*first);
    image[36] = std::byte{}; // Clearing an old pixel is a change too.
    submit(&a);
    auto held = take();
    is(*held, a);
    image[180] = std::byte{0x17};
    submit(&b); // Commit during an active read must survive its completion.
    apply(*held);
    auto next = take();
    is(*next, {1, 1, 5, 5});
    apply(*next);
    image[36] = std::byte{0x28};
    submit(&a);
    auto failed = take();
    failed->complete(make_result(OWH_AGENT_NOT_READY));
    image[180] = std::byte{};
    submit(&b);
    auto recovered = take();
    is(*recovered, {1, 1, 5, 5});
    apply(*recovered);

    image[36] = std::byte{0x39};
    submit(&a);
    if (policy == OWH_QUEUE_LATEST_READY) {
        auto aborted = producer->prepare_submit(); // Displace A, then abandon its replacement.
        assert(aborted.result.status == OWH_OK && aborted.displaced);
        assert(producer->abort(aborted.frame).status == OWH_OK);
    }
    image[180] = std::byte{0x4a};
    submit(&b);
    auto pending = take();
    is(*pending, {1, 1, 5, 5});
    apply(*pending);
    if (policy == OWH_QUEUE_FIFO) {
        auto last = take();
        apply(*last);
    }
    assert(retained == image);
    submit(nullptr); // Unhinted/direct submissions restore full damage.
    auto full = take();
    is(*full, {0, 0, 8, 8});
    apply(*full);

    // Completion can be deferred by metadata contention. A later commit must
    // remain dirty when the old completion is eventually flushed.
    submit(&a);
    auto deferred = take();
    auto &mutex = const_cast<Header &>(producer->header()).mutex;
    mutex.lock();
    deferred->complete(make_result(OWH_OK));
    mutex.unlock();
    submit(&b);
    auto after_deferred = take();
    is(*after_deferred, {1, 1, 5, 5});
    apply(*after_deferred);

    // Flushing completion of the newest image restores precise hints.
    submit(&a);
    auto newest = take();
    mutex.lock();
    newest->complete(make_result(OWH_OK));
    mutex.unlock();
    assert(consumer->try_take_ready().first.status == OWH_NO_DATA);
    submit(&b);
    auto precise = take();
    is(*precise, b);
    apply(*precise);
    auto invalid = producer->try_acquire();
    assert(invalid.result.status == OWH_OK);
    const core::PixelRegion outside{UINT32_MAX, 0, 2, 1};
    assert(producer->commit(invalid.frame, &outside).result.status == OWH_INVALID_ARGUMENT);
    assert(producer->abort(invalid.frame).status == OWH_OK);
}
void local() {
    auto [r, p] = queue(OWH_QUEUE_LATEST_READY);
    assert(r.status == OWH_OK);
    auto [c, consumer] = imported(*p);
    assert(c.status == OWH_OK);
    auto a = p->try_acquire();
    assert(a.result.status == OWH_OK);
    std::fill(a.frame.pixels.begin(), a.frame.pixels.end(), std::byte{0x11});
    assert(p->commit(a.frame).result.status == OWH_OK);
    auto b = p->try_acquire();
    assert(b.result.status == OWH_OK);
    std::fill(b.frame.pixels.begin(), b.frame.pixels.end(), std::byte{0x22});
    assert(p->commit(b.frame).result.status == OWH_OK);
    auto [t, read] = consumer->try_take_ready();
    assert(t.status == OWH_OK && read->pixels()[0] == std::byte{0x22});
    assert(consumer->settle_for_close().status == OWH_BUSY);
    read->complete(make_result(OWH_OK));
    read.reset();
    uint32_t count = 0;
    auto done = p->collect_completed(count);
    assert(count == 2);
    bool replaced = false, published = false;
    for (uint32_t i = 0; i < count; ++i) {
        replaced |= done[i].result.status == OWH_REPLACED;
        published |= done[i].published;
        assert(done[i].resource_released);
    }
    assert(replaced && published);
    auto held = p->try_acquire();
    assert(held.result.status == OWH_OK);
    assert(consumer->settle_for_close().status == OWH_BUSY);
    assert(p->abort(held.frame).status == OWH_OK);
    assert(consumer->settle_for_close().status == OWH_OK);
    assert(p->try_acquire().result.status == OWH_CLOSED);
    auto [r2, p2] = queue();
    assert(r2.status == OWH_OK);
    auto descriptor = p2->descriptor();
    descriptor.ref.resource_id++;
    assert(PixelConsumer::import(descriptor, GetCurrentProcess(), p2->descriptor().ref, 2, 2, 3,
                                 OWH_QUEUE_FIFO, 1 << 20)
               .first.status == OWH_INVALID_ARGUMENT);
    DWORD baseline = 0, after = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &baseline));
    descriptor = p2->descriptor();
    descriptor.completion_handle = UINT64_MAX - 1;
    assert(PixelConsumer::import(descriptor, GetCurrentProcess(), descriptor.ref, 2, 2, 3,
                                 OWH_QUEUE_FIFO, 1 << 20)
               .first.status != OWH_OK);
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    assert(after == baseline);
    auto &header = const_cast<Header &>(p2->header());
    header.width = 3;
    assert(p2->try_acquire().result.status == OWH_CORRUPT);
    header.width = 2;
}
int child(HANDLE input) {
    QueueDescriptor d{};
    DWORD n = 0;
    if (!ReadFile(input, &d, sizeof(d), &n, nullptr) || n != sizeof(d))
        return 2;
    Handle producer(
        OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                    d.producer_pid));
    if (!producer)
        return 3;
    auto [r, c] = PixelConsumer::import(d, producer.get(), d.ref, d.width, d.height, d.slot_count,
                                        d.queue_policy, 1 << 20);
    if (r.status != OWH_OK)
        return 4;
    if (WaitForSingleObject(c->wake_event(), 5000) != WAIT_OBJECT_0)
        return 5;
    auto [t, read] = c->try_take_ready();
    if (t.status != OWH_OK || !read || read->pixels().size() != 16 ||
        read->pixels()[0] != std::byte{0x5a})
        return 6;
    read->complete(make_result(OWH_OK));
    return 0;
}
int producer_child(HANDLE output, HANDLE release, uint32_t parent_pid) {
    Handle parent(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent_pid));
    assert(parent);
    auto [status, producer] = PixelProducer::create({99, 7}, parent_pid, started(parent.get()), 2,
                                                    2, 2, OWH_QUEUE_FIFO, 1 << 20);
    assert(status.status == OWH_OK);
    // Exit with a writable lease outstanding, after the other process imports.
    assert(producer->try_acquire().result.status == OWH_OK);
    const auto descriptor = producer->descriptor();
    DWORD bytes = 0;
    assert(WriteFile(output, &descriptor, sizeof(descriptor), &bytes, nullptr) &&
           bytes == sizeof(descriptor));
    uint32_t acknowledgement = 0;
    assert(ReadFile(release, &acknowledgement, sizeof(acknowledgement), &bytes, nullptr) &&
           bytes == sizeof(acknowledgement));
    return acknowledgement == 1 ? 0 : 2;
}
void producer_death() {
    SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
    HANDLE descriptor_read = nullptr, descriptor_write = nullptr, ack_read = nullptr,
           ack_write = nullptr;
    assert(CreatePipe(&descriptor_read, &descriptor_write, &inherited, 0));
    assert(CreatePipe(&ack_read, &ack_write, &inherited, 0));
    Handle incoming(descriptor_read), outgoing(descriptor_write), child_ack(ack_read),
        ack(ack_write);
    assert(SetHandleInformation(incoming.get(), HANDLE_FLAG_INHERIT, 0));
    assert(SetHandleInformation(ack.get(), HANDLE_FLAG_INHERIT, 0));
    wchar_t executable[32768]{};
    assert(GetModuleFileNameW(nullptr, executable, 32768));
    auto command = L"\"" + std::wstring(executable) + L"\" producer " +
                   std::to_wstring(reinterpret_cast<uintptr_t>(outgoing.get())) + L" " +
                   std::to_wstring(reinterpret_cast<uintptr_t>(child_ack.get())) + L" " +
                   std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION pi{};
    assert(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &pi));
    Handle process(pi.hProcess), thread(pi.hThread);
    outgoing.reset();
    child_ack.reset();
    QueueDescriptor descriptor{};
    DWORD bytes = 0;
    assert(ReadFile(incoming.get(), &descriptor, sizeof(descriptor), &bytes, nullptr) &&
           bytes == sizeof(descriptor));
    auto [status, consumer] = PixelConsumer::import(descriptor, process.get(), descriptor.ref, 2, 2,
                                                    2, OWH_QUEUE_FIFO, 1 << 20);
    assert(status.status == OWH_OK);
    const uint32_t release = 1;
    assert(WriteFile(ack.get(), &release, sizeof(release), &bytes, nullptr) &&
           bytes == sizeof(release));
    assert(WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0);
    DWORD exit_code = 99;
    assert(GetExitCodeProcess(process.get(), &exit_code) && exit_code == 0);
    assert(consumer->try_take_ready().first.status == OWH_AGENT_LOST);
    assert(consumer->settle_for_close().status == OWH_OK);
}
void child_process() {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE raw_read = nullptr, raw_write = nullptr;
    assert(CreatePipe(&raw_read, &raw_write, &sa, 0));
    Handle input(raw_read), output(raw_write);
    assert(SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0));
    wchar_t exe[32768]{};
    assert(GetModuleFileNameW(nullptr, exe, 32768));
    std::wstring command = L"\"" + std::wstring(exe) + L"\" child " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(input.get()));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION pi{};
    assert(CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                          nullptr, &startup, &pi));
    Handle process(pi.hProcess), thread(pi.hThread);
    input.reset();
    auto [r, p] = PixelProducer::create({88, 4}, pi.dwProcessId, started(process.get()), 2, 2, 2,
                                        OWH_QUEUE_FIFO, 1 << 20);
    assert(r.status == OWH_OK);
    const auto d = p->descriptor();
    DWORD n = 0;
    assert(WriteFile(output.get(), &d, sizeof(d), &n, nullptr) && n == sizeof(d));
    auto frame = p->try_acquire();
    assert(frame.result.status == OWH_OK);
    std::fill(frame.frame.pixels.begin(), frame.frame.pixels.end(), std::byte{0x5a});
    assert(p->commit(frame.frame).result.status == OWH_OK);
    assert(WaitForSingleObject(process.get(), 7000) == WAIT_OBJECT_0);
    DWORD code = 99;
    assert(GetExitCodeProcess(process.get(), &code) && code == 0);
    uint32_t count = 0;
    auto done = p->collect_completed(count);
    assert(count == 1 && done[0].published);
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 5)
        return producer_child(reinterpret_cast<HANDLE>(std::stoull(argv[2])),
                              reinterpret_cast<HANDLE>(std::stoull(argv[3])),
                              static_cast<uint32_t>(std::stoul(argv[4])));
    if (argc == 3)
        return child(reinterpret_cast<HANDLE>(std::stoull(argv[2])));
    managed_admission();
    accumulated_damage(OWH_QUEUE_LATEST_READY);
    accumulated_damage(OWH_QUEUE_FIFO);
    local();
    wake_failure_after_claim();
    child_process();
    producer_death();
    std::cout << "producer/import ownership, latest-ready, leases, import rollback, post-handoff "
                 "wake failure and child-process transfer passed\n";
}
