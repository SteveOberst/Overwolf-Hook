import dev.sxlver.overwolfhook.OverwolfHook

fun main() {
    OverwolfHook.load()
    val info = OverwolfHook.buildInfo()
    println("Overwolf Hook ${info.sdkVersion.utf8()} loaded " +
        "(build ${info.buildNumber})")
}
