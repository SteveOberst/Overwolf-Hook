package examples.state;

import dev.sxlver.overwolfhook.GameStateController;
import dev.sxlver.overwolfhook.NativeFailure;
import dev.sxlver.overwolfhook.model.ByteView;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class ReadState {
    private static String name(ByteView bytes) {
        var text = new StringBuilder("\"");
        for (long i = 0; i < Math.min(bytes.size(), 64); i++) {
            int b = Byte.toUnsignedInt(bytes.get(i));
            if (b >= 32 && b <= 126 && b != '"' && b != '\\')
                text.append((char)b);
            else
                text.append(String.format("\\x%02x", b));
        }
        if (bytes.size() > 64)
            text.append("...");
        return text.append('"').toString();
    }

    private static boolean matches(ByteView actual, String expected) {
        return Arrays.equals(actual.copyBytes(),
                             expected.getBytes(StandardCharsets.US_ASCII));
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 0 && args.length != 2)
            throw new IllegalArgumentException(
                "Usage: ReadState [CATEGORY KEY]");
        int matches = 0;
        var state =
            GameStateController.open(); // Passive: no SDK context or attach.
        var channels = state.sources()
                           .stream()
                           .filter(channel
                                   -> (channel.getKinds() &
                                       OwhConstants.OWH_CHANNEL_INFO) != 0)
                           .toList();
        if (channels.isEmpty())
            System.out.println("No state (INFO) channels were discovered.");
        NativeFailure firstFailure = null;
        for (var channel : channels) {
            try {
                var snapshot = state.read(channel);
                if (snapshot.fields().isEmpty())
                    System.out.printf(
                        "channel=%d: snapshot contains zero fields.%n",
                        channel.getSourceChannel());
                for (var field : snapshot.fields()) {
                    if (args.length == 2 &&
                        (!matches(field.getCategory(), args[0]) ||
                         !matches(field.getKey(), args[1])))
                        continue;
                    matches++;
                    System.out.printf(
                        "channel=%d category=%s key=%s valueBytes=%d "
                            + "firstBytes=",
                        channel.getSourceChannel(), name(field.getCategory()),
                        name(field.getKey()), field.getBytes().size());
                    var value = field.getBytes();
                    for (long i = 0; i < Math.min(value.size(), 16); i++)
                        System.out.printf("%02x ",
                                          Byte.toUnsignedInt(value.get(i)));
                    System.out.println(value.size() > 16 ? "..." : "");
                }
            } catch (NativeFailure failure) {
                System.err.println("Channel " + channel.getSourceChannel() +
                                   ": " + failure);
                if (firstFailure == null)
                    firstFailure = failure;
            }
        }
        if (args.length == 2 && matches == 0)
            System.out.println("Requested field not present.");
        if (firstFailure != null)
            throw firstFailure;
    }
}
