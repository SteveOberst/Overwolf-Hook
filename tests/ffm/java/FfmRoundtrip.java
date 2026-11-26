import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.*;

/** Raw Java FFM check for ABI exports and independently owned SDK contexts; never attaches. */
final class FfmRoundtrip {
    private static final MemoryLayout RESULT = MemoryLayout.structLayout(JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG);
    private static MethodHandle function(SymbolLookup library, String name, FunctionDescriptor descriptor) {
        return Linker.nativeLinker().downcallHandle(library.find(name).orElseThrow(
            () -> new AssertionError("Missing ABI export " + name)), descriptor);
    }
    private static int status(MemorySegment result) { return result.get(JAVA_INT, 0); }

    public static void main(String[] args) throws Throwable {
        if (args.length != 1) throw new IllegalArgumentException("Expected the ABI client DLL path");
        try (Arena arena = Arena.ofConfined()) {
            var allocator = (SegmentAllocator) arena;
            var client = SymbolLookup.libraryLookup(Path.of(args[0]), arena);
            var version = function(client, "owh_sdk_abi_version", FunctionDescriptor.of(JAVA_INT));
            if ((int) version.invokeExact() != 1) throw new AssertionError("Unexpected SDK ABI version");

            var open = function(client, "owh_sdk_open", FunctionDescriptor.of(RESULT, ADDRESS));
            var close = function(client, "owh_sdk_close", FunctionDescriptor.ofVoid(ADDRESS));
            var firstOut = arena.allocate(ADDRESS);
            var secondOut = arena.allocate(ADDRESS);
            firstOut.set(ADDRESS, 0, MemorySegment.ofAddress(0x1234));
            secondOut.set(ADDRESS, 0, MemorySegment.ofAddress(0x5678));
            var firstResult = (MemorySegment) open.invokeExact(allocator, firstOut);
            if (status(firstResult) != 0) throw new AssertionError("SDK open failed: " + status(firstResult));
            var first = firstOut.get(ADDRESS, 0);
            var secondResult = (MemorySegment) open.invokeExact(allocator, secondOut);
            if (status(secondResult) != 0) throw new AssertionError("Second SDK open failed: " + status(secondResult));
            var second = secondOut.get(ADDRESS, 0);
            if (first.equals(MemorySegment.NULL) || second.equals(MemorySegment.NULL) || first.equals(second))
                throw new AssertionError("SDK contexts were not independently allocated");
            close.invokeExact(first);
            close.invokeExact(second);
            System.out.println("PASS Java FFM ABI smoke: independent SDK contexts (no host attach)");
        }
    }
}
