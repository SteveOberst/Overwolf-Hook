import dev.sxlver.overwolfhook.NativeFailure
import dev.sxlver.overwolfhook.OverwolfHook
import java.util.HexFormat
import dev.sxlver.overwolfhook.kotlin.readState
import kotlinx.coroutines.runBlocking
import kotlin.time.Duration.Companion.milliseconds

fun main(args: Array<String>) {
    if (args.firstOrNull() == "events") {
        require(args.size <= 2) { "Usage: events [Overwolf PID]" }
        runBlocking { receiveEvents(args.getOrNull(1)?.toLong()) }
        return
    }
    val hook = OverwolfHook.open()
    for (channel in hook.channels().filter { it.info }) {
        if (args.isNotEmpty() && channel.id != args.single().toLong()) continue
        try {
            for (field in hook.readState(channel.id, 100.milliseconds).fields) {
                println("${channel.id} ${field.categoryText()}.${field.keyText()}: ${HexFormat.of().formatHex(field.bytes)}")
            }
        } catch (unavailable: NativeFailure) {
            System.err.println(unavailable.message)
        }
    }
}
