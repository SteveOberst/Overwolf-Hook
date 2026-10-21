"""Independent files verify long-capture inspection and corruption detection."""
import importlib.util
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location("inspector", Path(__file__).parents[1] / "tools/inspect_capture.py")
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def record(sequence, gap=0, channel=123):
    frame = struct.pack("<QII", 123456, channel, 255 + 63 * 1024)
    frame += b"opaque\0" + bytes(255 - 7) + b"\xff\0" + bytes(63 * 1024 - 2) + b"\0"
    return struct.pack("<QQII", sequence, gap, len(frame), 0) + frame


with tempfile.TemporaryDirectory(prefix="owh-inspect-") as temporary:
    path = Path(temporary) / "events.bin"
    path.write_bytes(record(5, 2) + record(6) + record(9, 2))
    result = inspector.inspect(path)
    assert result["records"] == 3 and result["first_sequence"] == 5 and result["last_sequence"] == 9
    assert result["bridge_overwrites"] == 4 and result["records_before_reader_retention_boundary"] == 2
    for invalid in [record(1)[:-1], record(1) + b"\0", record(1) + record(3), record(1, channel=0)]:
        path.write_bytes(invalid)
        try:
            inspector.inspect(path)
        except ValueError:
            pass
        else:
            raise AssertionError("Corrupt recording accepted")
print("PASS inspector raw framing, gaps, truncation and source identity")
