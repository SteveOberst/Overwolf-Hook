package examples.advanced;

import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.model.ClientOptions;
import dev.sxlver.overwolfhook.model.OwhConstants;

/**
 * Advanced: attach to an already running Overwolf process without restarting
 * it.
 */
public final class HotAttach {
    public static void main(String[] args) throws Exception {
        try (var sdk = OverwolfHook.open()) {
            // --8<-- [start:attach]
            var options =
                new ClientOptions().setAttachMode(OwhConstants.OWH_ATTACH_HOT);
            sdk.attach(options);
            // --8<-- [end:attach]
            try {
                System.out.println(sdk.agentInfo());
            } finally {
                sdk.detach(); // Ends the SDK session; Overwolf keeps running.
            }
        }
    }
}
