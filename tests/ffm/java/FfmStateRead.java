import java.lang.foreign.*;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

/** One passive InfoDB read, preserving raw field bytes for the state fixture. */
public final class FfmStateRead {
    private static final MemoryLayout RESULT = MemoryLayout.structLayout(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG);
    public static void main(String[] args) throws Throwable {
        if (args.length != 2) throw new IllegalArgumentException("FfmStateRead <client DLL> <source channel>");
        long channel = Long.parseUnsignedLong(args[1]);
        if (channel < 1 || channel > 0xffff_ffffL) throw new IllegalArgumentException("Invalid channel");
        try (Arena arena = Arena.ofConfined()) {
            var lookup = SymbolLookup.libraryLookup(Path.of(args[0]).toAbsolutePath(), arena);
            var linker = Linker.nativeLinker();
            var read = linker.downcallHandle(lookup.find("owh_game_state_read").orElseThrow(),
                FunctionDescriptor.of(RESULT, JAVA_INT, JAVA_INT, ADDRESS));
            var count = linker.downcallHandle(lookup.find("owh_snapshot_count").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
            var at = linker.downcallHandle(lookup.find("owh_snapshot_at").orElseThrow(), FunctionDescriptor.of(RESULT, ADDRESS, JAVA_INT, ADDRESS));
            var closeSnapshot = linker.downcallHandle(lookup.find("owh_snapshot_close").orElseThrow(), FunctionDescriptor.ofVoid(ADDRESS));
            var output = arena.allocate(ADDRESS);
            check((MemorySegment)read.invokeExact((SegmentAllocator)arena, (int)channel, 100, output), "state read");
            var snapshot = output.get(ADDRESS, 0);
            try {
                int fields = (int)count.invokeExact(snapshot);
                var field = arena.allocate(40, 8);
                System.out.println("fields=" + fields);
                for (int i = 0; i < fields; ++i) {
                    check((MemorySegment)at.invokeExact((SegmentAllocator)arena, snapshot, i, field), "read snapshot field");
                    var category = field.get(ADDRESS, 0).reinterpret(4_194_304L).getString(0);
                    var key = field.get(ADDRESS, 8).reinterpret(4_194_304L).getString(0);
                    int size = field.get(JAVA_INT, 32);
                    var bytes = size == 0 ? new byte[0] : field.get(ADDRESS, 16).reinterpret(Integer.toUnsignedLong(size)).toArray(JAVA_BYTE);
                    System.out.println(category + "." + key + " ticks=" + Long.toUnsignedString(field.get(JAVA_LONG, 24))
                        + " hex=" + java.util.HexFormat.of().formatHex(bytes));
                }
            } finally { closeSnapshot.invokeExact(snapshot); }
        }
    }
    private static void check(MemorySegment result, String action) {
        int status = result.get(JAVA_INT, 0);
        if (status != 0) throw new IllegalStateException(action + " status=" + status + ", stage=" + result.get(JAVA_INT, 4) +
            ", domain=" + result.get(JAVA_INT, 8) + ", code=0x" + Long.toUnsignedString(result.get(JAVA_LONG, 16), 16));
    }
}
