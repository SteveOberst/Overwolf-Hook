package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.Capture
import dev.sxlver.overwolfhook.EventReader
import dev.sxlver.overwolfhook.OverwolfHook
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.toJavaDuration

/** Blocking receive with a Kotlin timeout. INFINITE has no deadline; finite values
 * must be non-negative whole milliseconds and obey the Java SDK's native bounds.
 */
fun Capture.receive(timeout: Duration): EventReader.Result =
    if (timeout == Duration.INFINITE) receive() else receive(timeout.wholeMilliseconds())

/** Same blocking receive contract as [Capture.receive], for an independent reader. */
fun EventReader.receive(timeout: Duration): EventReader.Result =
    if (timeout == Duration.INFINITE) receive() else receive(timeout.wholeMilliseconds())

/** Explicit blocking state read; the SDK retains its 0..1000 ms lock-timeout bound. */
fun OverwolfHook.readState(sourceChannel: Long, lockTimeout: Duration): OverwolfHook.State =
    readState(sourceChannel, lockTimeout.wholeMilliseconds())

internal fun Duration.wholeMilliseconds(): java.time.Duration {
    require(isFinite() && this >= Duration.ZERO && inWholeMilliseconds.milliseconds == this) {
        "Timeout must be finite, non-negative whole milliseconds"
    }
    return toJavaDuration()
}
