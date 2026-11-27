package examples.diagnostics

import dev.sxlver.overwolfhook.OverwolfHook

fun main() {
    OverwolfHook.open().use { sdk ->
        val report = sdk.check()
        println("Checked: ${report.checked}")
        println("Passed: ${report.passed}")
        println("Failed: ${report.checked and report.passed.inv()}")
    }
}
