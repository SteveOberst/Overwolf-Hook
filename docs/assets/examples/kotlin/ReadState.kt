package examples.state

import dev.sxlver.overwolfhook.GameStateController
import dev.sxlver.overwolfhook.NativeFailure
import dev.sxlver.overwolfhook.model.ByteView
import dev.sxlver.overwolfhook.model.OwhConstants

private fun name(bytes: ByteView): String = buildString {
    append('"')
    for (i in 0 until minOf(bytes.size(), 64L)) {
        val b = bytes.get(i).toInt() and 0xff
        if (b in 32..126 && b != '"'.code && b != '\\'.code) append(b.toChar())
        else append("\\x%02x".format(b))
    }
    if (bytes.size() > 64) append("...")
    append('"')
}

private fun matches(actual: ByteView, expected: String): Boolean =
    actual.copyBytes().contentEquals(expected.toByteArray(Charsets.US_ASCII))

fun main(args: Array<String>) {
    require(args.isEmpty() || args.size == 2) {
        "Usage: ReadState [CATEGORY KEY]"
    }
    var matches = 0
    val state = GameStateController.open() // Passive: no SDK context or attach.
    val channels = state.sources().filter {
        (it.kinds and OwhConstants.OWH_CHANNEL_INFO) != 0L
    }
    if (channels.isEmpty()) println("No state (INFO) channels were discovered.")
    var firstFailure: NativeFailure? = null
    for (channel in channels) {
        try {
            val snapshot = state.read(channel)
            if (snapshot.fields().isEmpty())
                println("channel=${channel.sourceChannel}: " +
                    "snapshot contains zero fields.")
            snapshot.fields().forEach { field ->
                if (args.size == 2 && (!matches(field.category, args[0]) ||
                        !matches(field.key, args[1]))) return@forEach
                matches++
                print("channel=${channel.sourceChannel} " +
                    "category=${name(field.category)} " +
                    "key=${name(field.key)} valueBytes=${field.bytes.size()} " +
                    "firstBytes=")
                val value = field.bytes
                for (i in 0 until minOf(value.size(), 16L))
                    print("%02x ".format(value.get(i).toInt() and 0xff))
                println(if (value.size() > 16) "..." else "")
            }
        } catch (failure: NativeFailure) {
            System.err.println("Channel ${channel.sourceChannel}: $failure")
            if (firstFailure == null) firstFailure = failure
        }
    }
    if (args.size == 2 && matches == 0) println("Requested field not present.")
    firstFailure?.let { throw it }
}
