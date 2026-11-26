import dev.sxlver.overwolfhook.OverwolfHook

fun main() {
    OverwolfHook.open().use {
        println("Overwolf Hook SDK context opened")
    }
}
