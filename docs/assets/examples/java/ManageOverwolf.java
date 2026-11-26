package examples.overwolf;

import dev.sxlver.overwolfhook.OverwolfController;

public final class ManageOverwolf {
    // --8<-- [start:installation]
    public static void showInstallation() throws Exception {
        var overwolf = OverwolfController.open();
        var installation = overwolf.installation();
        if (installation.isPresent()) {
            var installed = installation.get();
            System.out.println(installed.getDirectory().utf8());
            System.out.println(installed.getLauncher().utf8());
            System.out.println(installed.getVersion().utf8());
        } else {
            System.out.println("Overwolf installation not found.");
        }
    }
    // --8<-- [end:installation]

    // --8<-- [start:process]
    public static void showProcess() throws Exception {
        var overwolf = OverwolfController.open();
        var process = overwolf.runningProcess();
        if (process.isPresent()) {
            System.out.println(process.get().getExecutable().utf8());
            System.out.println(process.get().getVersion().utf8());
        }
    }
    // --8<-- [end:process]

    // --8<-- [start:start]
    public static void startOverwolf() throws Exception {
        var overwolf = OverwolfController.open();
        var process = overwolf.start(); // Default timeout: 10 seconds.
        System.out.println("Overwolf is running: PID=" +
                           process.getProcessId());
    }
    // --8<-- [end:start]

    // --8<-- [start:stop]
    public static void stopOverwolf() throws Exception {
        var overwolf = OverwolfController.open();
        var process = overwolf.runningProcess();
        if (process.isPresent()) {
            overwolf.stop(process.get()); // Default timeout: 10 seconds.
        }
    }
    // --8<-- [end:stop]
}
