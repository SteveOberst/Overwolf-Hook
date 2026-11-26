package dev.sxlver.overwolfhook;

/** The live event subscription ended at its native source. */
public final class EventStreamClosedException extends RuntimeException {
    public EventStreamClosedException() { super("Event subscription is closed"); }
}
