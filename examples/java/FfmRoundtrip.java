import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;

import static java.lang.foreign.ValueLayout.*;

/** Run with Java 25 and the absolute capture/client DLL paths as arguments.
 *  Demonstrates the C ABI on a private channel without touching Overwolf.
 */
class FfmRoundtrip {
    private static final MemoryLayout RESULT = MemoryLayout.structLayout(JAVA_INT, JAVA_INT);
    private static final MemoryLayout RECORD = MemoryLayout.structLayout(JAVA_LONG, JAVA_LONG, JAVA_INT, JAVA_INT);

    private static MethodHandle function(SymbolLookup library, String name, FunctionDescriptor descriptor) {
        return Linker.nativeLinker().downcallHandle(library.find(name).orElseThrow(), descriptor);
    }

    private static void success(MemorySegment result) {
        if (result.get(JAVA_INT, 0) != 0)
            throw new AssertionError("Native status " + result.get(JAVA_INT, 0) + ", Win32 " + result.get(JAVA_INT, 4));
    }

    public static void main(String[] args) throws Throwable {
        if (args.length != 2) throw new IllegalArgumentException("Expected capture DLL and client DLL paths");
        try (Arena arena = Arena.ofConfined()) {
            SymbolLookup capture = SymbolLookup.libraryLookup(Path.of(args[0]), arena);
            SymbolLookup client = SymbolLookup.libraryLookup(Path.of(args[1]), arena);
            MethodHandle create = function(capture, "oeh_writer_create",
                FunctionDescriptor.of(RESULT, JAVA_INT, ADDRESS, JAVA_INT, JAVA_INT, ADDRESS));
            MethodHandle publish = function(capture, "oeh_writer_publish",
                FunctionDescriptor.of(RESULT, ADDRESS, ADDRESS, JAVA_INT));
            MethodHandle closeWriter = function(capture, "oeh_writer_close", FunctionDescriptor.of(RESULT, ADDRESS));
            MethodHandle open = function(client, "oeh_reader_open",
                FunctionDescriptor.of(RESULT, JAVA_INT, ADDRESS, JAVA_INT, ADDRESS));
            MethodHandle next = function(client, "oeh_reader_next",
                FunctionDescriptor.of(RESULT, ADDRESS, ADDRESS, JAVA_INT, ADDRESS));
            MethodHandle closeReader = function(client, "oeh_reader_close", FunctionDescriptor.ofVoid(ADDRESS));
            MemorySegment name = arena.allocateFrom("ffm-" + ProcessHandle.current().pid());
            MemorySegment writerOut = arena.allocate(ADDRESS);
            MemorySegment readerOut = arena.allocate(ADDRESS);
            success((MemorySegment) create.invoke(arena, 1, name, 8, 64, writerOut));
            MemorySegment writer = writerOut.get(ADDRESS, 0);
            MemorySegment reader = MemorySegment.NULL;
            try {
                success((MemorySegment) open.invoke(arena, 1, name, 0, readerOut));
                reader = readerOut.get(ADDRESS, 0);
                MemorySegment payload = arena.allocate(JAVA_INT);
                payload.set(JAVA_INT, 0, 0x12345678);
                success((MemorySegment) publish.invoke(arena, writer, payload, 4));
                MemorySegment output = arena.allocate(JAVA_INT);
                MemorySegment record = arena.allocate(RECORD);
                success((MemorySegment) next.invoke(arena, reader, output, 4, record));
                if (output.get(JAVA_INT, 0) != 0x12345678 || record.get(JAVA_LONG, 0) != 1 ||
                    record.get(JAVA_LONG, 8) != 0 || record.get(JAVA_INT, 16) != 4)
                    throw new AssertionError("FFM payload/record layout mismatch");
            } finally {
                closeReader.invoke(reader);
                success((MemorySegment) closeWriter.invoke(arena, writer));
            }
            System.out.println("PASS Java FFM C ABI roundtrip");
        }
    }
}
