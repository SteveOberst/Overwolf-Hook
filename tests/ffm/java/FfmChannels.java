import java.lang.foreign.*;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

/** Passive source discovery without an SDK context, attach, or agent action. */
public final class FfmChannels {
    private static final MemoryLayout RESULT = MemoryLayout.structLayout(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG);
    public static void main(String[] args) throws Throwable {
        if (args.length < 1 || args.length > 2) throw new IllegalArgumentException("FfmChannels <client DLL> [expected channel]");
        try (Arena arena = Arena.ofConfined()) {
            var symbols = SymbolLookup.libraryLookup(Path.of(args[0]).toAbsolutePath(), arena);
            var linker = Linker.nativeLinker();
            var sources = linker.downcallHandle(symbols.find("owh_game_state_sources").orElseThrow(), FunctionDescriptor.of(RESULT, ADDRESS));
            var count = linker.downcallHandle(symbols.find("owh_source_list_count").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
            var at = linker.downcallHandle(symbols.find("owh_source_list_at").orElseThrow(), FunctionDescriptor.of(RESULT, ADDRESS, JAVA_INT, ADDRESS));
            var closeList = linker.downcallHandle(symbols.find("owh_source_list_close").orElseThrow(), FunctionDescriptor.ofVoid(ADDRESS));
            var output = arena.allocate(ADDRESS);
            check((MemorySegment)sources.invokeExact((SegmentAllocator)arena, output), "source discovery");
            var channels = output.get(ADDRESS, 0);
            try {
                var row = arena.allocate(12, 4);
                int length = (int)count.invokeExact(channels);
                boolean expected = args.length == 1;
                for (int i = 0; i < length; ++i) {
                    check((MemorySegment)at.invokeExact((SegmentAllocator)arena, channels, i, row), "read source row");
                    long id = Integer.toUnsignedLong(row.get(JAVA_INT, 0));
                    int kinds = row.get(JAVA_INT, 4);
                    long producer = Integer.toUnsignedLong(row.get(JAVA_INT, 8));
                    System.out.println("source_channel=" + id + " producer_process_id=" + producer + " info=" + ((kinds & 1) != 0) + " events=" + ((kinds & 2) != 0));
                    if (args.length == 2 && id == Long.parseUnsignedLong(args[1]) && (kinds & 1) != 0) expected = true;
                }
                if (!expected) throw new IllegalStateException("Expected fixture channel missing");
            } finally { closeList.invokeExact(channels); }
        }
    }
    private static void check(MemorySegment result, String action) {
        int status = result.get(JAVA_INT, 0);
        if (status != 0) throw new IllegalStateException(action + " status=" + status + ", stage=" + result.get(JAVA_INT, 4) +
            ", domain=" + result.get(JAVA_INT, 8) + ", code=0x" + Long.toUnsignedString(result.get(JAVA_LONG, 16), 16));
    }
}
