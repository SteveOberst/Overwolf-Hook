import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.captureEvents
import kotlinx.coroutines.flow.collect

/** Collect until the producer ends or the caller cancels this coroutine. */
suspend fun receiveEvents(processId: Long? = null) {
    OverwolfHook.open().captureEvents(processId).collect { event ->
        println("channel=${event.sourceChannel} sequence=${event.sequence} lost=${event.lostBefore}")
    }
}
