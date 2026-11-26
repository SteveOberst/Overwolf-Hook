package examples.events

import dev.sxlver.overwolfhook.EventStreamClosedException
import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.model.Event
import dev.sxlver.overwolfhook.kotlin.events
import dev.sxlver.overwolfhook.kotlin.receive
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

// --8<-- [start:handler]
private fun handleEvent(event: Event) {
    print("channel=${event.sourceChannel} " +
        "rawBytes=${event.raw.size()} " +
        "lost=${event.lostBefore.toULong()} firstBytes=")
    for (i in 0 until minOf(event.raw.size(), 16L))
        print("%02x ".format(event.raw.get(i).toInt() and 0xff))
    println(if (event.raw.size() > 16) "..." else "")
}
// --8<-- [end:handler]

fun main() {
    OverwolfHook.open().use { sdk ->
        // Default restart attach may close and relaunch Overwolf.
        sdk.attach()
        try {
            sdk.events.subscribe().use { subscription ->
                println("Listening for 5 minutes. " +
                    "Enter the game and generate events now.")
                val started = TimeSource.Monotonic.markNow()
                while (started.elapsedNow() < 5.minutes) {
                    val event = subscription.receive(1.seconds).orElse(null)
                        ?: continue
                    handleEvent(event)
                }
                println("Observation ended.")
            }
        } catch (closed: EventStreamClosedException) {
            println("Event stream closed.")
        } finally {
            sdk.detach()
        }
    }
}
