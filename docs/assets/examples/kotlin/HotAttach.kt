package examples.advanced

import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.model.ClientOptions
import dev.sxlver.overwolfhook.model.OwhConstants

/** Advanced HOT attach to a running Overwolf process. */
fun main() {
    OverwolfHook.open().use { sdk ->
        // --8<-- [start:attach]
        val options = ClientOptions().setAttachMode(OwhConstants.OWH_ATTACH_HOT)
        sdk.attach(options)
        // --8<-- [end:attach]
        try {
            println(sdk.agentInfo())
        } finally {
            sdk.detach() // Ends the SDK session; Overwolf keeps running.
        }
    }
}
