package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.EventReader
import dev.sxlver.overwolfhook.NativeFailure
import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.bindings.OverwolfNative.*
import dev.sxlver.overwolfhook.bindings.owh_probe_frame
import dev.sxlver.overwolfhook.bindings.owh_result
import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlin.test.*
import kotlin.time.Duration
import kotlin.time.Duration.Companion.days
import kotlin.time.Duration.Companion.microseconds
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

class EventsTest {
    @Test fun `finite Kotlin durations preserve precision and SDK bounds`() {
        assertEquals(java.time.Duration.ofMillis(123), 123.milliseconds.wholeMilliseconds())
        for (invalid in listOf((-1).milliseconds, 1.microseconds, Duration.INFINITE, -Duration.INFINITE))
            assertFailsWith<IllegalArgumentException> { invalid.wholeMilliseconds() }
        NativeChannel().use { channel ->
            channel.open().use { reader ->
                assertEquals(EventReader.Status.TIMEOUT, reader.receive(Duration.ZERO))
                assertFailsWith<IllegalArgumentException> { reader.receive(50.days) }
                reader.cancel()
                assertEquals(EventReader.Status.CANCELLED, reader.receive(Duration.INFINITE))
            }
            assertFailsWith<IllegalArgumentException> { NativeChannel.hook.readState(1L, 1001.milliseconds) }
        }
    }

    @Test fun `owned capture is lazy and startup failures remain failures`(): Unit = runBlocking {
        NativeChannel().use {
            val stream = NativeChannel.hook.captureEvents(-1)
            assertFailsWith<IllegalArgumentException> { stream.toList() }
        }
    }

    @Test fun `flows are lazy and readers retain independent cursors`(): Unit = runBlocking {
        NativeChannel().use { channel ->
            val opened = Channel<Unit>(2)
            val stream = eventStream { channel.open().also { opened.trySend(Unit).getOrThrow() } }
            assertTrue(opened.tryReceive().isFailure)
            val first = async { stream.take(2).toList() }
            val second = async { stream.take(2).toList() }
            withTimeout(2.seconds) { repeat(2) { opened.receive() } }
            channel.publish(11); channel.publish(22)
            val a = withTimeout(2.seconds) { first.await() }
            val b = withTimeout(2.seconds) { second.await() }
            assertEquals(listOf(1L, 2L), a.map { it.sequence })
            assertEquals(a.map { it.sequence }, b.map { it.sequence })
            assertContentEquals(a[0].bytes, b[0].bytes)
            assertEquals(900001L, a[0].sourceChannel)
            assertEquals(11, a[0].bytes[0].toInt())
        }
    }

    @Test fun `cancelling a silent collector closes only its own reader`(): Unit = runBlocking {
        NativeChannel().use { channel ->
            val firstReader = CompletableDeferred<EventReader>()
            val secondReader = CompletableDeferred<EventReader>()
            val first = launch {
                eventStream { channel.open().also { firstReader.complete(it) } }.collect { error("Unexpected event") }
            }
            val second = async {
                eventStream { channel.open().also { secondReader.complete(it) } }.take(1).toList()
            }
            val reader = withTimeout(2.seconds) { firstReader.await() }
            withTimeout(2.seconds) { secondReader.await() }
            first.cancel()
            withTimeout(2.seconds) { first.join() }
            assertFailsWith<IllegalStateException> { reader.poll() }
            channel.publish(7)
            assertEquals(7, withTimeout(2.seconds) { second.await() }.single().bytes[0].toInt())
        }
    }

    @Test fun `producer closure drains retained frames with exact overwrite counts`(): Unit = runBlocking {
        NativeChannel(slots = 2).use { channel ->
            val opened = CompletableDeferred<Unit>()
            val release = CountDownLatch(1)
            val received = async {
                eventStream {
                    channel.open().also { opened.complete(Unit); release.await() }
                }.toList()
            }
            try {
                withTimeout(2.seconds) { opened.await() }
                repeat(4) { channel.publish(it + 1) }
                channel.closeWriter()
            } finally { release.countDown() }
            val events = withTimeout(2.seconds) { received.await() }
            assertEquals(listOf(3L, 4L), events.map { it.sequence })
            assertEquals(listOf(2L, 0L), events.map { it.lostBefore })
            assertEquals(listOf(3, 4), events.map { it.bytes[0].toInt() })
        }
    }

    @Test fun `unavailable bridge propagates failure rather than completing empty`(): Unit = runBlocking {
        NativeChannel().use {
            val failure = assertFailsWith<NativeFailure> {
                eventStream { NativeChannel.hook.events("kotlin-missing-${System.nanoTime()}") }.toList()
            }
            assertEquals(OWH_NOT_FOUND(), failure.status())
        }
    }

    @Test fun `collector failure is preserved and reader is closed`(): Unit = runBlocking {
        NativeChannel().use { channel ->
            channel.publish(9)
            var reader: EventReader? = null
            val failure = IllegalStateException("collector failed")
            val caught = assertFailsWith<IllegalStateException> {
                eventStream { channel.open().also { reader = it } }.collect { throw failure }
            }
            assertEquals(failure.message, caught.message)
            // Coroutine stacktrace recovery can copy standard exceptions.
            assertTrue(generateSequence<Throwable>(caught) { it.cause }.any { it === failure })
            assertFailsWith<IllegalStateException> { reader!!.poll() }
        }
    }
}

/** Our own generic bridge and writer: no Overwolf/game process or third-party DLL. */
private class NativeChannel(slots: Int = 8) : AutoCloseable {
    companion object {
        val hook = OverwolfHook.open()
        private val identities = AtomicLong()
        init { System.load(requireNotNull(System.getProperty("owhCaptureDll")) { "Set -PowhCaptureDll to the source-built capture DLL" }) }
    }
    private val arena = Arena.ofConfined()
    private val name = "kotlin-test-${ProcessHandle.current().pid()}-${identities.incrementAndGet()}"
    private var writer: MemorySegment
    init {
        val output = arena.allocate(ValueLayout.ADDRESS)
        check(owh_result.status(owh_writer_create(arena, OWH_ABI_VERSION(), arena.allocateFrom(name), slots,
            owh_probe_frame.sizeof().toInt(), output)) == OWH_OK())
        writer = output.get(ValueLayout.ADDRESS, 0)
    }
    fun open(): EventReader = hook.events(name)
    fun publish(value: Int) {
        val frame = owh_probe_frame.allocate(arena)
        owh_probe_frame.source_channel(frame, 900001)
        owh_probe_frame.captured_qpc(frame, value.toLong())
        owh_probe_frame.raw_size(frame, owh_probe_frame.raw(frame).byteSize().toInt())
        owh_probe_frame.raw(frame).fill(value.toByte())
        retryBusy { owh_result.status(owh_writer_publish(arena, writer, frame, frame.byteSize().toInt())) }
    }
    fun closeWriter() {
        if (writer == MemorySegment.NULL) return
        retryBusy { owh_result.status(owh_writer_close(arena, writer)) }
        writer = MemorySegment.NULL
    }
    private fun retryBusy(operation: () -> Int) {
        val deadline = System.nanoTime() + 1_000_000_000L
        while (true) {
            val status = operation()
            if (status == OWH_OK()) return
            check(status == OWH_BUSY() && System.nanoTime() < deadline) { "Native test operation failed: $status" }
            Thread.yield()
        }
    }
    override fun close() { try { closeWriter() } finally { arena.close() } }
}
