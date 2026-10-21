package dev.sxlver.overwolfhook;

import java.io.IOException;
import java.lang.foreign.*;
import java.nio.file.*;
import java.util.concurrent.TimeUnit;
import static java.lang.foreign.ValueLayout.*;

/** Explicit capture lifetime. Closing requests stop/drain; a Windows job kills only our controller if the JVM dies.
 * The hook's owner watcher then stops interception in Overwolf. The game process is never a target.
 * No callback thread, implicit retry or game-state interpretation is imposed on the caller.
 */
public final class Capture implements AutoCloseable {
    private final ControllerJob owner;
    private final Process controller;
    private final Path directory;
    private final Path stop;
    private final Path log;
    private EventReader reader;
    private boolean closed;

    private Capture(ControllerJob owner, Process controller, Path directory) {
        this.owner = owner;
        this.controller = controller;
        this.directory = directory;
        stop = directory.resolve("STOP");
        log = directory.resolve("controller.log");
    }

    static java.util.OptionalLong findHost() throws IOException {
        try (var processes = ProcessHandle.allProcesses()) {
            var candidates = processes.filter(process -> process.info().command().map(command ->
                Path.of(command).getFileName().toString().equalsIgnoreCase("Overwolf.exe")).orElse(false) &&
                java.util.Arrays.stream(process.info().arguments().orElse(new String[0])).noneMatch(arg -> arg.startsWith("--type="))).toList();
            if (candidates.size() > 1) throw new IOException("Multiple Overwolf hosts; select a process explicitly");
            return candidates.isEmpty() ? java.util.OptionalLong.empty() : java.util.OptionalLong.of(candidates.getFirst().pid());
        }
    }

    static long runningHost() throws IOException {
        return findHost().orElseThrow(() -> new IOException("Overwolf is not running"));
    }

    static Capture start(Path binaries, long processId, Path recording) throws IOException {
        if (processId <= 0 || processId > 0xffff_ffffL) throw new IllegalArgumentException("Process ID must be uint32 and nonzero");
        if (recording != null && Files.exists(recording)) throw new FileAlreadyExistsException(recording.toString());
        var directory = Files.createTempDirectory("overwolf-hook-session-");
        ControllerJob owner = null;
        Capture session = null;
        try {
            owner = new ControllerJob();
            var arguments = new java.util.ArrayList<String>();
            arguments.add(binaries.resolve("owh_probe.exe").toString());
            if (recording == null) arguments.add("stream");
            arguments.add(Long.toString(processId));
            arguments.add(binaries.resolve("owh_capture.dll").toString());
            if (recording != null) {
                arguments.add("0");
                arguments.add(recording.toAbsolutePath().toString());
            }
            arguments.addAll(java.util.List.of("all", "--stop-file", directory.resolve("STOP").toString()));
            var builder = new ProcessBuilder(arguments)
                .redirectErrorStream(true).redirectOutput(directory.resolve("controller.log").toFile());
            session = new Capture(owner, owner.start(builder), directory);
            long started = System.nanoTime();
            while (true) {
                String diagnostics = session.diagnostics();
                for (String line : diagnostics.lines().toList()) {
                    if (line.startsWith("hook enabled; channel=")) {
                        String channel = line.substring("hook enabled; channel=".length()).split(";", 2)[0];
                        session.reader = new EventReader(channel, false);
                        return session;
                    }
                }
                if (!session.controller.isAlive()) throw new IOException("Overwolf capture did not start: " + diagnostics);
                if (System.nanoTime() - started > TimeUnit.SECONDS.toNanos(35)) throw new IOException("Overwolf capture startup timed out: " + diagnostics);
                Thread.sleep(10);
            }
        } catch (Throwable failure) {
            if (failure instanceof InterruptedException) Thread.currentThread().interrupt();
            try {
                if (session != null) session.close();
                else {
                    if (owner != null) owner.close();
                    Files.deleteIfExists(directory.resolve("controller.log"));
                    Files.deleteIfExists(directory);
                }
            } catch (Throwable cleanup) { failure.addSuppressed(cleanup); }
            if (failure instanceof IOException io) throw io;
            if (failure instanceof Error error) throw error;
            throw new IOException("Capture startup failed", failure);
        }
    }

    public synchronized EventReader.Result poll() throws NativeFailure {
        if (closed) throw new IllegalStateException("Capture is closed");
        return reader.poll();
    }

    /** Startup/profile diagnostics and QPC/UTC anchor. Available until close; recordings are never deleted. */
    public String diagnostics() throws IOException { return Files.exists(log) ? Files.readString(log) : ""; }

    @Override public synchronized void close() throws IOException {
        if (closed) return;
        closed = true;
        boolean interrupted = Thread.interrupted();
        IOException completion = null;
        try {
            try {
                Files.createFile(stop);
                if (!controller.waitFor(10, TimeUnit.SECONDS)) {
                    completion = new IOException("Capture did not drain before its shutdown deadline: " + diagnostics());
                    controller.destroyForcibly();
                } else if (controller.exitValue() != 0) {
                    completion = new IOException("Capture controller failed: " + diagnostics());
                }
            } catch (InterruptedException signal) { interrupted = true; }
            finally {
                try { owner.close(); }
                finally {
                    try { if (reader != null) reader.close(); }
                    finally {
                        try { controller.waitFor(2, TimeUnit.SECONDS); }
                        catch (InterruptedException signal) { interrupted = true; }
                    }
                }
            }
        } finally {
            try {
                Files.deleteIfExists(stop);
                Files.deleteIfExists(log);
                Files.deleteIfExists(directory);
            } finally { if (interrupted) Thread.currentThread().interrupt(); }
        }
        if (completion != null) throw completion;
    }

    /** Exact Windows x64 job ABI. This controls our child, not the Overwolf process. */
    private static final class ControllerJob implements AutoCloseable {
        private final Arena arena = Arena.ofShared();
        private final SymbolLookup kernel = SymbolLookup.libraryLookup("kernel32", arena);
        private MemorySegment job = MemorySegment.NULL;
        private MemorySegment call(String name, FunctionDescriptor descriptor, Object... arguments) {
            try { return (MemorySegment)Linker.nativeLinker().downcallHandle(kernel.find(name).orElseThrow(), descriptor).invokeWithArguments(arguments); }
            catch (Throwable failure) { throw new IllegalStateException(name, failure); }
        }
        private int number(String name, FunctionDescriptor descriptor, Object... arguments) {
            try { return (int)Linker.nativeLinker().downcallHandle(kernel.find(name).orElseThrow(), descriptor).invokeWithArguments(arguments); }
            catch (Throwable failure) { throw new IllegalStateException(name, failure); }
        }
        ControllerJob() {
            try {
                job = call("CreateJobObjectW", FunctionDescriptor.of(ADDRESS, ADDRESS, ADDRESS), MemorySegment.NULL, MemorySegment.NULL);
                if (job.equals(MemorySegment.NULL)) throw new IllegalStateException("Cannot create controller lifetime job");
                var limits = arena.allocate(144, 8).fill((byte)0); // JOBOBJECT_EXTENDED_LIMIT_INFORMATION, Windows SDK
                limits.set(JAVA_INT, 16, 0x2000); // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                if (number("SetInformationJobObject", FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_INT), job, 9, limits, 144) == 0)
                    throw new IllegalStateException("Cannot configure controller lifetime job");
            } catch (Throwable failure) { close(); throw failure; }
        }
        Process start(ProcessBuilder builder) throws IOException {
            Process process = builder.start();
            try {
                var child = call("OpenProcess", FunctionDescriptor.of(ADDRESS, JAVA_INT, JAVA_INT, JAVA_INT), 0x101, 0, (int)process.pid());
                if (child.equals(MemorySegment.NULL)) throw new IllegalStateException("Cannot open controller process");
                try {
                    if (number("AssignProcessToJobObject", FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS), job, child) == 0)
                        throw new IllegalStateException("Cannot own controller process");
                } finally { number("CloseHandle", FunctionDescriptor.of(JAVA_INT, ADDRESS), child); }
                return process;
            } catch (Throwable failure) { process.destroyForcibly(); throw failure; }
        }
        @Override public void close() {
            if (!arena.scope().isAlive()) return;
            try { if (!job.equals(MemorySegment.NULL)) number("CloseHandle", FunctionDescriptor.of(JAVA_INT, ADDRESS), job); }
            finally { arena.close(); }
        }
    }
}
