import java.lang.foreign.*;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

/** One passive InfoDB read. No code is loaded into Overwolf by this example. */
public class FfmStateRead {
    public static void main(String[] args) throws Throwable {
        if (args.length != 2) throw new IllegalArgumentException("FfmStateRead <client DLL> <source channel>");
        long channel = Long.parseUnsignedLong(args[1]);
        if (channel < 1 || channel > 0xffffffffL) throw new IllegalArgumentException("Invalid channel");
        try (Arena arena = Arena.ofConfined()) {
            var lookup = SymbolLookup.libraryLookup(Path.of(args[0]).toAbsolutePath(), arena);
            var linker = Linker.nativeLinker();
            var result = MemoryLayout.structLayout(JAVA_INT, JAVA_INT);
            var read = linker.downcallHandle(lookup.find("owh_state_read").orElseThrow(),
                FunctionDescriptor.of(result, JAVA_INT, JAVA_INT, JAVA_INT, ADDRESS));
            var count = linker.downcallHandle(lookup.find("owh_state_count").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
            var at = linker.downcallHandle(lookup.find("owh_state_at").orElseThrow(), FunctionDescriptor.of(result, ADDRESS, JAVA_INT, ADDRESS));
            var close = linker.downcallHandle(lookup.find("owh_state_close").orElseThrow(), FunctionDescriptor.ofVoid(ADDRESS));
            var output = arena.allocate(ADDRESS);
            var status = (MemorySegment) read.invoke(arena, 1, (int)channel, 100, output);
            if (status.get(JAVA_INT, 0) != 0) throw new IllegalStateException("status=" + status.get(JAVA_INT, 0) + ", Win32=" + status.get(JAVA_INT, 4));
            var state = output.get(ADDRESS, 0);
            try {
                int fields = (int)count.invoke(state);
                var field = arena.allocate(40, 8);
                System.out.println("fields=" + fields);
                for (int i = 0; i < fields; ++i) {
                    status = (MemorySegment) at.invoke(arena, state, i, field);
                    if (status.get(JAVA_INT, 0) != 0) throw new IllegalStateException("Invalid field");
                    var category = field.get(ADDRESS, 0).reinterpret(257).getString(0);
                    var key = field.get(ADDRESS, 8).reinterpret(257).getString(0);
                    int size = field.get(JAVA_INT, 32);
                    var bytes = size == 0 ? new byte[0] : field.get(ADDRESS, 16).reinterpret(size).toArray(JAVA_BYTE);
                    System.out.println(category + "." + key + " ticks=" + Long.toUnsignedString(field.get(JAVA_LONG, 24))
                        + " hex=" + java.util.HexFormat.of().formatHex(bytes));
                }
            } finally { close.invoke(state); }
        }
    }
}
