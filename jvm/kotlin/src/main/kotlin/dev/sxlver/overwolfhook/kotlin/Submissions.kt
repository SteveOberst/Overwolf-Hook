package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.Submission
import dev.sxlver.overwolfhook.WaitCancellation
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext
import kotlin.time.Duration

suspend fun Submission.awaitCancellable(timeout: Duration = Duration.INFINITE): dev.sxlver.overwolfhook.model.SubmissionInfo = coroutineScope {
    val nativeTimeout = timeout.optionalJavaTimeout()
    WaitCancellation.create().use { token ->
        val waiter = async(Dispatchers.IO) { await(nativeTimeout, token) }
        try { waiter.await() }
        finally {
            withContext(NonCancellable) {
                try { token.cancel() }
                finally { waiter.join() }
            }
        }
    }
}
