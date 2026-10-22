package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.EventReader
import dev.sxlver.overwolfhook.OverwolfHook
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds

/** Child-JVM integration harness launched by tests/jvm_capture_test.py against our synthetic host. */
fun main(args: Array<String>) = runBlocking {
    val hook = OverwolfHook.open()
    val pid = args[1].toLong()
    if (args[0] == "silent") {
        withContext(Dispatchers.IO) {
            hook.capture(pid).use { capture ->
                withTimeoutOrNull(150.milliseconds) { capture.events.collect { error("Unexpected event") } }
                // Cancelling a Flow cancels its own reader, not the capture's primary cursor.
                check(capture.receive(Duration.ZERO) == EventReader.Status.TIMEOUT)
            }
        }
        withTimeoutOrNull(500.milliseconds) { hook.captureEvents(pid).collect { error("Unexpected event") } }
    } else {
        withContext(Dispatchers.IO) {
            hook.capture(pid).use { capture ->
                coroutineScope {
                    val first = async { capture.events.take(2).toList() }
                    val second = async { capture.events.take(2).toList() }
                    val (a, b) = withTimeout(3.seconds) { first.await() to second.await() }
                    check(a.map { it.sequence } == b.map { it.sequence })
                    check(a.first().bytes.contentEquals(b.first().bytes))
                    check(capture.receive(Duration.ZERO) is EventReader.Event)
                }
            }
        }
        val events = withTimeout(3.seconds) { hook.captureEvents(pid).take(3).toList() }
        check(events.size == 3 && events.all { it.sourceChannel == 900001L })
    }
    ProcessHandle.current().children().use { check(it.noneMatch { child -> child.isAlive }) }
    println("PASS Kotlin ${args[0]}: independent Capture flows, primary reader and controller ownership")
}
