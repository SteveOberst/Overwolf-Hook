import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.events
import dev.sxlver.overwolfhook.kotlin.receive
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.time.Duration.Companion.seconds

/** Borrow one existing capture with two independent Flow readers. Closing either
 * flow leaves the capture alive; this outer scope owns its eventual shutdown.
 */
suspend fun shareCapture(processId: Long) = withContext(Dispatchers.IO) {
    OverwolfHook.open().capture(processId).use { capture ->
        coroutineScope {
            launch { capture.events.take(3).collect { println("first: ${it.sequence}") } }
            launch { capture.events.take(3).collect { println("second: ${it.sequence}") } }
        }
        // Kotlin Duration overload on the original cursor, independent of both flows.
        println(capture.receive(1.seconds))
    }
}
