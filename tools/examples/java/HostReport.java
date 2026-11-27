package examples.diagnostics;

import dev.sxlver.overwolfhook.OverwolfHook;

public class HostReport {
    public static void main(String[] args) throws Exception {
        try (var sdk = OverwolfHook.open()) {
            var report = sdk.check();
            System.out.println("Checked: " + report.getChecked());
            System.out.println("Passed: " + report.getPassed());
            System.out.println("Failed: " + (report.getChecked() & ~report.getPassed()));
        }
    }
}
