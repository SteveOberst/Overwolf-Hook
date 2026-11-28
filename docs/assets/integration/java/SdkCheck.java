import dev.sxlver.overwolfhook.OverwolfHook;

public class SdkCheck {
    public static void main(String[] args) throws Exception {
        OverwolfHook.load();
        var info = OverwolfHook.buildInfo();
        System.out.println("Overwolf Hook " + info.getSdkVersion().utf8()
            + " loaded (build " + info.getBuildNumber() + ")");
    }
}
