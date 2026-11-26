package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.EventController
import dev.sxlver.overwolfhook.EventStreamClosedException
import dev.sxlver.overwolfhook.EventSubscription
import dev.sxlver.overwolfhook.model.Event
import dev.sxlver.overwolfhook.model.EventOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.withContext
import java.time.Duration

/** Each collection owns one subscription and closes it after its bounded receive loop. */
fun EventController.observe(options: EventOptions = EventOptions()): Flow<Event> = flow {
    var subscription: EventSubscription? = null
    var failure: Throwable? = null
    try {
        val active = subscribe(options)
        subscription = active
        while (true) {
            currentCoroutineContext().ensureActive()
            val event = active.receive(Duration.ofMillis(100))
            if (event.isPresent) emit(event.get())
        }
    } catch (_: EventStreamClosedException) {
        // Native source stopped; close this collection normally.
    } catch (cancelled: kotlinx.coroutines.CancellationException) {
        failure = cancelled
        throw cancelled
    } catch (error: Throwable) {
        failure = error
        throw error
    } finally {
        val owned = subscription
        if (owned != null) withContext(NonCancellable) {
            try { owned.close() }
            catch (cleanup: Throwable) {
                if (failure != null) failure.addSuppressed(cleanup) else throw cleanup
            }
        }
    }
}.flowOn(Dispatchers.IO)
