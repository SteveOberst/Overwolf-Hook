package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.Capture
import dev.sxlver.overwolfhook.EventReader
import dev.sxlver.overwolfhook.OverwolfHook
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.buffer
import kotlinx.coroutines.flow.channelFlow
import kotlinx.coroutines.flow.emitAll
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.launch
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext

/** Cold stream with an independent retained reader per collector. Getting the flow
 * performs no I/O. Collection runs reads on Dispatchers.IO and closes only its own
 * reader on completion, failure or cancellation; the caller still owns this Capture.
 * Producer closure completes after retained records drain. Native failures propagate.
 * There is no extra event buffer by default; a slow collector can still overrun the
 * bounded native ring, reported by Event.lostBefore. Cancellation may discard in-flight data.
 */
val Capture.events: Flow<EventReader.Event> get() = eventStream(::openReader)

/** Cold, owned capture: each collection starts its own capture and closes it on exit.
 * Omit processId to discover the single host, or select it explicitly. Startup and
 * shutdown run on Dispatchers.IO. Concurrent captures on the same host are rejected
 * by the native SDK; use one Capture.events with separate collectors to share a host.
 */
fun OverwolfHook.captureEvents(processId: Long? = null): Flow<EventReader.Event> = flow {
    var owned: Capture? = null
    try {
        // Retain ownership inside the interruptible block: cancellation after
        // construction must not discard a returned Capture before it can close.
        runInterruptible { owned = if (processId == null) capture() else capture(processId) }
        emitAll(checkNotNull(owned).events)
    } finally { owned?.close() }
}.buffer(Channel.RENDEZVOUS).flowOn(Dispatchers.IO)

internal fun eventStream(openReader: () -> EventReader): Flow<EventReader.Event> = channelFlow {
    ensureActive()
    openReader().use { reader ->
        val receiver = launch(Dispatchers.IO) {
            while (true) {
                ensureActive()
                when (val result = reader.receive()) {
                    is EventReader.Event -> send(result)
                    EventReader.Status.CLOSED, EventReader.Status.CANCELLED -> break
                    else -> error("Unexpected blocking receive status: $result")
                }
            }
        }
        try { receiver.join() }
        finally {
            // Interrupting the Java thread cannot wake WaitForMultipleObjects.
            // Signal native cancellation, then join before closing its native arena.
            try { reader.cancel() }
            finally { withContext(NonCancellable) { receiver.join() } }
        }
    }
}.buffer(Channel.RENDEZVOUS).flowOn(Dispatchers.IO)
