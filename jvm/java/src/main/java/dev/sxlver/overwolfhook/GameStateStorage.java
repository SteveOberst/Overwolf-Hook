package dev.sxlver.overwolfhook;

/** Lightweight source descriptor; each read obtains a fresh owned snapshot. */
public final class GameStateStorage {
    private final GameStateController controller;
    private final long sourceChannel;
    private final long producerProcessId;
    GameStateStorage(GameStateController controller, long sourceChannel, long producerProcessId) {
        this.controller = controller;
        this.sourceChannel = sourceChannel;
        this.producerProcessId = producerProcessId;
    }
    public long sourceChannel() { return sourceChannel; }
    public long producerProcessId() { return producerProcessId; }
    public StateSnapshot read() throws NativeFailure { return read(dev.sxlver.overwolfhook.model.OwhConstants.OWH_STATE_DEFAULT_TIMEOUT_MS); }
    public StateSnapshot read(long timeoutMillis) throws NativeFailure { return controller.read(sourceChannel, timeoutMillis, producerProcessId); }
}
