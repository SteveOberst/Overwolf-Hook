import dev.sxlver.overwolfhook.OverwolfHook;

public class SdkCheck {
    public static void main(String[] args) throws Exception {
        try (var sdk = OverwolfHook.open()) {
            System.out.println("Overwolf Hook SDK context opened");
        }
    }
}
