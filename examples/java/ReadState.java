import dev.sxlver.overwolfhook.NativeFailure;
import dev.sxlver.overwolfhook.OverwolfHook;
import java.util.HexFormat;

/** Passive access only; a field's presence does not certify an active match. */
public class ReadState {
    public static void main(String[] args) throws Exception {
        var hook = OverwolfHook.open();
        for (var channel : hook.channels()) {
            if (!channel.info()) continue;
            if (args.length != 0 && channel.id() != Long.parseUnsignedLong(args[0])) continue;
            try {
                var state = hook.readState(channel.id());
                for (var field : state.fields()) {
                    System.out.printf("%d %s.%s ticks=%s hex=%s%n", state.sourceChannel(),
                        field.categoryText(), field.keyText(), Long.toUnsignedString(field.producerTicks()),
                        HexFormat.of().formatHex(field.bytes()));
                }
            } catch (NativeFailure unavailable) {
                // Sources can disappear after enumeration; preserve the native failure instead of printing an empty state.
                System.err.println(unavailable.getMessage());
            }
        }
    }
}
