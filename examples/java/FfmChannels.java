import java.lang.foreign.*;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

/** Passive discovery; the optional second argument asserts a known fixture channel for tests. */
public class FfmChannels {
    public static void main(String[] args) throws Throwable {
        if (args.length < 1 || args.length > 2) throw new IllegalArgumentException("FfmChannels <client DLL> [expected channel]");
        try (Arena arena = Arena.ofConfined()) {
            var symbols = SymbolLookup.libraryLookup(Path.of(args[0]).toAbsolutePath(), arena);
            var linker = Linker.nativeLinker();
            var result = MemoryLayout.structLayout(JAVA_INT, JAVA_INT);
            var discover = linker.downcallHandle(symbols.find("owh_channels_discover").orElseThrow(), FunctionDescriptor.of(result, JAVA_INT, ADDRESS));
            var count = linker.downcallHandle(symbols.find("owh_channels_count").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
            var at = linker.downcallHandle(symbols.find("owh_channels_at").orElseThrow(), FunctionDescriptor.of(result, ADDRESS, JAVA_INT, ADDRESS));
            var close = linker.downcallHandle(symbols.find("owh_channels_close").orElseThrow(), FunctionDescriptor.ofVoid(ADDRESS));
            var output = arena.allocate(ADDRESS);
            var status = (MemorySegment) discover.invoke(arena, 1, output);
            if (status.get(JAVA_INT, 0) != 0) throw new IllegalStateException("Discovery status=" + status.get(JAVA_INT, 0) + ", Win32=" + status.get(JAVA_INT, 4));
            var channels = output.get(ADDRESS, 0);
            try {
                var row = arena.allocate(8, 4);
                int length = (int)count.invoke(channels);
                boolean expected = args.length == 1;
                for (int i = 0; i < length; ++i) {
                    status = (MemorySegment) at.invoke(arena, channels, i, row);
                    if (status.get(JAVA_INT, 0) != 0) throw new IllegalStateException("Invalid channel row");
                    long id = Integer.toUnsignedLong(row.get(JAVA_INT, 0));
                    int kinds = row.get(JAVA_INT, 4);
                    System.out.println("source_channel=" + id + " info=" + ((kinds & 1) != 0) + " events=" + ((kinds & 2) != 0));
                    if (args.length == 2 && id == Long.parseUnsignedLong(args[1]) && (kinds & 1) != 0) expected = true;
                }
                if (!expected) throw new IllegalStateException("Expected fixture channel missing");
            } finally { close.invoke(channels); }
        }
    }
}
