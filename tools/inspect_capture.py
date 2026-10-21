"""Inspect the experimental x64 probe recording without interpreting game semantics."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

RECORD = struct.Struct("<QQII")
FRAME = struct.Struct("<QII")
RAW_SIZE = 255 + 63 * 1024
FRAME_SIZE = (FRAME.size + RAW_SIZE + 7) & ~7  # Reviewed native x64 alignment.


def inspect(path: Path):
    names = Counter()
    count = 0
    first_sequence = None
    channels = set()
    lost = 0
    previous = 0
    unavailable_prefix = 0
    digest = hashlib.sha256()
    first_qpc = last_qpc = None
    with path.open("rb") as source:
        while header := source.read(RECORD.size):
            if len(header) != RECORD.size:
                raise ValueError("Truncated record header")
            sequence, gap, size, reserved = RECORD.unpack(header)
            if size != FRAME_SIZE or reserved or sequence <= previous:
                raise ValueError("Invalid probe record layout or order")
            frame = source.read(size)
            if len(frame) != size:
                raise ValueError("Truncated probe frame")
            qpc, channel, raw_size = FRAME.unpack_from(frame)
            expected_gap = sequence - previous - 1
            if not channel or raw_size != RAW_SIZE or gap > expected_gap or (previous and expected_gap != gap):
                raise ValueError("Inconsistent raw size or sequence-gap accounting")
            if not previous:
                unavailable_prefix = expected_gap - gap
            raw = frame[FRAME.size:FRAME.size + raw_size]
            # Names are a display projection; the file retains the complete bytes.
            name = raw[:255].split(b"\0", 1)[0].decode("utf-8", errors="backslashreplace")
            names[name] += 1
            count += 1
            if first_sequence is None:
                first_sequence = sequence
            channels.add(channel)
            lost += gap
            if first_qpc is None:
                first_qpc = qpc
            last_qpc = qpc
            previous = sequence
            digest.update(header)
            digest.update(frame)
    return {
        "format": "owh experimental x64 record + frame, ABI 1",
        "records": count,
        "first_sequence": first_sequence,
        "last_sequence": previous if count else None,
        "bridge_overwrites": lost,
        "records_before_reader_retention_boundary": unavailable_prefix,
        "channels": sorted(channels),
        "event_names": dict(sorted(names.items())),
        "first_captured_qpc": first_qpc,
        "last_captured_qpc": last_qpc,
        "sha256": digest.hexdigest(),
        "upstream_loss": "not measured",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.capture), indent=2))
