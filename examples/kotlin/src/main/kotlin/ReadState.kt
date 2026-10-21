import dev.sxlver.overwolfhook.NativeFailure
import dev.sxlver.overwolfhook.OverwolfHook
import java.util.HexFormat

fun main(args: Array<String>) {
    if (args.singleOrNull() == "events") {
        receiveEvents()
        return
    }
    val hook = OverwolfHook.open()
    for (channel in hook.channels().filter { it.info() }) {
        if (args.isNotEmpty() && channel.id() != args.single().toLong()) continue
        try {
            for (field in hook.readState(channel.id()).fields()) {
                println("${channel.id()} ${field.categoryText()}.${field.keyText()}: ${HexFormat.of().formatHex(field.bytes())}")
            }
        } catch (unavailable: NativeFailure) {
            System.err.println(unavailable.message)
        }
    }
}
