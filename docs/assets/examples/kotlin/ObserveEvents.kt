package examples.events.flow

import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.events
import dev.sxlver.overwolfhook.kotlin.observe
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.time.Duration.Companion.minutes

/** A bounded console collection; UI applications collect in their own scope. */
fun main(): Unit = runBlocking {
    OverwolfHook.open().use { sdk ->
        sdk.attach() // Default restart: run before entering the game.
        try {
            println("Collecting for 5 minutes. Enter the game now.")
            withTimeoutOrNull(5.minutes) {
                sdk.events.observe().collect { event ->
                    // Call your UI/model handler here with the owned Event.
                    println("channel=${event.sourceChannel} " +
                        "bytes=${event.raw.size()} " +
                        "knownLoss=${event.lostBefore.toULong()}")
                }
            } // Cancellation closes this collection's subscription.
        } finally {
            sdk.detach()
        }
    }
}
