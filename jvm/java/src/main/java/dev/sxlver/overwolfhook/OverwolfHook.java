package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import dev.sxlver.overwolfhook.model.AgentInfo;
import dev.sxlver.overwolfhook.model.BuildInfo;
import dev.sxlver.overwolfhook.model.ClientOptions;
import dev.sxlver.overwolfhook.model.ClientSanityReport;
import java.io.IOException;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Files;
import java.nio.file.Path;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** One independently attachable native SDK context. Only the DLL loader is shared. */
public final class OverwolfHook implements AutoCloseable {
    private static Path loadedDirectory;
    private final NativeHandle handle;
    private final OverlayController overlayController = new OverlayController(this);
    private final EventController eventController = new EventController(this);
    private final GameStateController gameStateController = new GameStateController();
    private final OverwolfController overwolfController = OverwolfController.loaded();

    private OverwolfHook(MemorySegment handle) { this.handle = new NativeHandle(handle); }

    /** Creates a local SDK context, loading the Client DLL if needed.
     * Does not connect to Overwolf; use {@link #attach()} for that. */
    public static OverwolfHook open() throws IOException {
        load();
        return openLoaded();
    }

    /** Loads the bundled Client DLL, or reuses the libraries already selected.
     * Extracts the matching Agent DLL for later attachment but does not load it
     * into Overwolf. Creates no SDK context or Agent connection. Repeated calls
     * reuse the selection for this class loader; no close operation is needed.
     * @throws IOException if extraction or ABI validation fails
     * @throws UnsupportedOperationException if the bundled platform is unsupported
     * @throws UnsatisfiedLinkError if the Client DLL or a dependency cannot load
     */
    public static synchronized void load() throws IOException {
        if (loadedDirectory == null) load(NativeBinaries.directory());
    }

    /** Loads the Client DLL from a directory containing the matching native pair.
     * Select this directory before the first native operation. Repeated calls
     * with the same canonical directory reuse it. Creates no SDK context or
     * Agent connection and does not load the Agent DLL into Overwolf.
     * @param directory the directory containing owh_client.dll and owh_agent.dll
     * @throws IOException if the directory or Agent payload is missing or ABI
     * validation fails
     * @throws IllegalStateException if a different directory is already selected
     * @throws UnsatisfiedLinkError if the Client DLL or a dependency cannot load
     */
    public static synchronized void load(Path directory) throws IOException {
        Path canonical = directory.toRealPath();
        if (!Files.isRegularFile(canonical.resolve("owh_agent.dll")))
            throw new IOException("Missing matching runtime payload: " + canonical.resolve("owh_agent.dll"));
        if (loadedDirectory != null && !loadedDirectory.equals(canonical))
            throw new IllegalStateException("A different native build is already loaded by this class loader");
        if (loadedDirectory == null) {
            System.load(canonical.resolve("owh_client.dll").toString());
            int abi = OverwolfNative.owh_sdk_abi_version();
            int expectedAbi = OverwolfNative.OWH_ABI_VERSION();
            if (abi != expectedAbi) throw new IOException("Unsupported native client ABI: expected " + expectedAbi + ", got " + abi);
            loadedDirectory = canonical;
        }
    }

    /** Creates a local SDK context using {@link #load(Path)}; does not attach. */
    public static OverwolfHook open(Path directory) throws IOException {
        load(directory);
        return openLoaded();
    }

    private static OverwolfHook openLoaded() throws IOException {
        try (var arena = Arena.ofConfined()) {
            var output = arena.allocate(ADDRESS);
            NativeResults.check("open SDK", OverwolfNative.owh_sdk_open(arena, output));
            MemorySegment sdk = output.get(ADDRESS, 0);
            if (sdk.equals(MemorySegment.NULL)) throw new IOException("Native SDK open returned no context");
            try { return new OverwolfHook(sdk); }
            catch (RuntimeException | Error failure) {
                NativeAdoption.cleanup(sdk, OverwolfNative::owh_sdk_close, failure);
                throw failure;
            }
        } catch (NativeFailure failure) { throw new IOException("Unable to open native SDK", failure); }
    }

    public OverwolfController overwolf() { return overwolfController; }

    public OverlayController overlays() { ensureOpen(); return overlayController; }
    public EventController events() { ensureOpen(); return eventController; }
    public GameStateController gameState() { return gameStateController; }

    /** Normally restarts Overwolf and requires the early startup boundary.
     * Does not force termination or silently fall back to hot attach.
     * Closing this context does not stop Overwolf. */
    public void attach() throws NativeFailure { attach(null); }
    /** Uses the native defaults unless options override them. Explicit
     * {@code OWH_ATTACH_HOT} leaves a running host alone and accepts an initial
     * initialization race that can crash Overwolf. */
    public void attach(ClientOptions options) throws NativeFailure {
        MemorySegment sdk = handle.get();
        try (var arena = Arena.ofConfined()) {
            MemorySegment input = options == null ? MemorySegment.NULL : options.materialize(arena);
            NativeResults.check("attach", OverwolfNative.owh_sdk_attach(arena, sdk, input));
        }
    }

    public void detach() throws NativeFailure {
        MemorySegment sdk = handle.get();
        try (var arena = Arena.ofConfined()) {
            NativeResults.check("detach", OverwolfNative.owh_sdk_detach(arena, sdk));
        }
    }

    public ClientSanityReport check() throws NativeFailure { return check(null); }
    public ClientSanityReport check(ClientOptions options) throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var output = ClientSanityReport.allocate(arena);
            MemorySegment input = options == null ? MemorySegment.NULL : options.materialize(arena);
            NativeResults.check("check client", OverwolfNative.owh_sdk_check(arena, input, output));
            return ClientSanityReport.copyOf(output);
        }
    }

    public AgentInfo probeAgent() throws NativeFailure { return probeAgent(null); }
    public AgentInfo probeAgent(ClientOptions options) throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var output = AgentInfo.allocate(arena);
            MemorySegment input = options == null ? MemorySegment.NULL : options.materialize(arena);
            NativeResults.check("probe agent", OverwolfNative.owh_sdk_probe_agent(arena, input, output));
            return AgentInfo.copyOf(output);
        }
    }

    public AgentInfo agentInfo() throws NativeFailure {
        MemorySegment sdk = handle.get();
        try (var arena = Arena.ofConfined()) {
            var output = AgentInfo.allocate(arena);
            NativeResults.check("read agent info", OverwolfNative.owh_sdk_agent_info(arena, sdk, output));
            return AgentInfo.copyOf(output);
        }
    }

    /** Returns the Client's SDK version and build number, loading it if needed.
     * Creates no SDK context or Agent connection. */
    public static BuildInfo buildInfo() throws IOException, NativeFailure {
        load();
        try (var arena = Arena.ofConfined()) {
            var output = BuildInfo.allocate(arena);
            NativeResults.check("read SDK build info", OverwolfNative.owh_sdk_build_info(arena, output));
            return BuildInfo.copyOf(output);
        }
    }

    public void ping(long timeoutMillis) throws NativeFailure {
        if (timeoutMillis < 0 || timeoutMillis > 0xffff_ffffL) throw new IllegalArgumentException("timeout outside uint32 range");
        MemorySegment sdk = handle.get();
        try (var arena = Arena.ofConfined()) {
            NativeResults.check("ping agent", OverwolfNative.owh_sdk_ping(arena, sdk, (int) timeoutMillis));
        }
    }

    NativeHandle handle() { return handle; }
    private void ensureOpen() { if (handle.closed()) throw new IllegalStateException("SDK context is closed"); }

    @Override public void close() {
        try { handle.close(h -> { OverwolfNative.owh_sdk_close(h); return MemorySegment.NULL; }); }
        catch (Exception failure) { throw new IllegalStateException("Unable to close SDK context", failure); }
    }
}
