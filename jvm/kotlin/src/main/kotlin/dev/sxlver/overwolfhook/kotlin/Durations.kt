package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.EventSubscription
import dev.sxlver.overwolfhook.GameStateStorage
import java.time.Duration as JavaDuration
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.toJavaDuration

fun EventSubscription.receive(timeout: Duration) = receive(timeout.wholeJavaDuration())
fun GameStateStorage.read(timeout: Duration) = read(timeout.wholeJavaDuration().toMillis())

internal fun Duration.wholeJavaDuration(): JavaDuration {
    require(isFinite() && this >= Duration.ZERO && inWholeMilliseconds.milliseconds == this && inWholeMilliseconds <= 4_294_967_294L) {
        "Timeout must be finite whole milliseconds from 0 to 4294967294"
    }
    return toJavaDuration()
}

/** Infinite Kotlin waits use Java's null timeout, never a rejected finite sentinel. */
internal fun Duration.optionalJavaTimeout(): JavaDuration? =
    if (this == Duration.INFINITE) null else wholeJavaDuration()
