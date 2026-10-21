import dev.sxlver.overwolfhook.EventReader
import dev.sxlver.overwolfhook.OverwolfHook
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

/** Explicit capture; no coroutine library is required. Call from an I/O thread in a UI application. */
fun receiveEvents() {
    val hook = OverwolfHook.open()
    hook.capture().use { capture ->
        val started = TimeSource.Monotonic.markNow()
        while (started.elapsedNow() < 10.seconds) {
            when (val received = capture.poll()) {
                is EventReader.Event -> println("channel=${received.sourceChannel()} sequence=${received.sequence()} lost=${received.lostBefore()}")
                EventReader.Status.CLOSED -> return
                EventReader.Status.NO_DATA, EventReader.Status.BUSY -> Thread.sleep(2)
            }
        }
    }
}
