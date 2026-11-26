package examples.overwolf

import dev.sxlver.overwolfhook.OverwolfController

// --8<-- [start:installation]
fun showInstallation() {
    val overwolf = OverwolfController.open()
    val installation = overwolf.installation().orElse(null)
    if (installation != null) {
        println(installation.directory.utf8())
        println(installation.launcher.utf8())
        println(installation.version.utf8())
    } else {
        println("Overwolf installation not found.")
    }
}
// --8<-- [end:installation]

// --8<-- [start:process]
fun showProcess() {
    val overwolf = OverwolfController.open()
    overwolf.runningProcess().ifPresent { process ->
        println(process.executable.utf8())
        println(process.version.utf8())
    }
}
// --8<-- [end:process]

// --8<-- [start:start]
fun startOverwolf() {
    val overwolf = OverwolfController.open()
    val process = overwolf.start() // Default timeout: 10 seconds.
    println("Overwolf is running: PID=${process.processId}")
}
// --8<-- [end:start]

// --8<-- [start:stop]
fun stopOverwolf() {
    val overwolf = OverwolfController.open()
    val process = overwolf.runningProcess().orElse(null)
    if (process != null) {
        overwolf.stop(process) // Default timeout: 10 seconds.
    }
}
// --8<-- [end:stop]
