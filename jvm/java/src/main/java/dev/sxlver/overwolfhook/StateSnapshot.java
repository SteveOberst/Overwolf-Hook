package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.model.StateField;
import java.util.List;

/** Owned immutable field values detached from the native snapshot mapping. */
public final class StateSnapshot {
    private final List<StateField> fields;
    StateSnapshot(List<StateField> fields) { this.fields = List.copyOf(fields); }
    public List<StateField> fields() { return fields; }
}
