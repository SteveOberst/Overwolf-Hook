package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.model.OverwolfInstallation;
import dev.sxlver.overwolfhook.model.OverwolfProcess;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.io.IOException;
import java.lang.foreign.Arena;
import java.util.Objects;
import java.util.Optional;

/** Local installation/process control. No SDK context or Agent attach is needed.
 * Start uses the installed launcher; it does not certify overlay readiness.
 */
public final class OverwolfController {
    private OverwolfController() { }
    /** Creates a local controller, loading the Client through
     * {@link OverwolfHook#load()} if needed. Does not attach to the Agent. */
    public static OverwolfController open() throws IOException {
        OverwolfHook.load();
        return loaded();
    }
    static OverwolfController loaded() { return new OverwolfController(); }

    public Optional<OverwolfInstallation> installation() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var out = OverwolfInstallation.allocate(arena);
            var result = OverwolfNative.owh_overwolf_installation_query(arena, out);
            if (NativeResults.status(result) == OwhConstants.OWH_NOT_FOUND)
                return Optional.empty();
            NativeResults.check("find Overwolf installation", result);
            return Optional.of(OverwolfInstallation.copyOf(out));
        }
    }
    public Optional<OverwolfProcess> runningProcess() throws NativeFailure {
        return runningProcess(0, 0);
    }
    public Optional<OverwolfProcess> runningProcess(long pid, long started) throws NativeFailure {
        if (pid < 0 || pid > 0xffff_ffffL) throw new IllegalArgumentException("PID outside uint32 range");
        try (var arena = Arena.ofConfined()) {
            var out = OverwolfProcess.allocate(arena);
            var result = OverwolfNative.owh_overwolf_process_query(arena, (int) pid, started, out);
            if (NativeResults.status(result) == OwhConstants.OWH_NOT_FOUND)
                return Optional.empty();
            NativeResults.check("find Overwolf process", result);
            return Optional.of(OverwolfProcess.copyOf(out));
        }
    }
    public OverwolfProcess start() throws NativeFailure { return start(10_000); }
    public OverwolfProcess start(long timeoutMillis) throws NativeFailure {
        int timeout = timeout(timeoutMillis);
        try (var arena = Arena.ofConfined()) {
            var out = OverwolfProcess.allocate(arena);
            NativeResults.check("start Overwolf", OverwolfNative.owh_overwolf_start(arena, timeout, out));
            return OverwolfProcess.copyOf(out);
        }
    }
    public void stop(OverwolfProcess process) throws NativeFailure { stop(process, 10_000); }
    public void stop(OverwolfProcess process, long timeoutMillis) throws NativeFailure {
        Objects.requireNonNull(process, "process");
        int timeout = timeout(timeoutMillis);
        try (var arena = Arena.ofConfined()) {
            NativeResults.check("stop Overwolf", OverwolfNative.owh_overwolf_stop(arena,
                (int) process.getProcessId(), process.getProcessStarted(), timeout));
        }
    }
    private static int timeout(long value) {
        if (value <= 0 || value >= 0xffff_ffffL) throw new IllegalArgumentException("timeout must be finite and positive");
        return (int) value;
    }
}
