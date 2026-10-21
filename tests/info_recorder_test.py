"""Exercise the shipped recorder against owned named objects, not a live provider."""
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.CreateFileMappingW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_wchar_p]
kernel.CreateFileMappingW.restype = ctypes.c_void_p
kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
kernel.CreateMutexW.restype = ctypes.c_void_p
kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t]
kernel.MapViewOfFile.restype = ctypes.c_void_p
kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]
kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]


class Source:
    def __init__(self, channel):
        name = f"ow_info_{channel}"
        self.mutex = kernel.CreateMutexW(None, False, name + "mtx")
        self.mapping = kernel.CreateFileMappingW(ctypes.c_void_p(-1), None, 4, 0, 4096, name)
        self.view = kernel.MapViewOfFile(self.mapping, 2, 0, 0, 4096)
        assert self.mutex and self.mapping and self.view
        self.write(1)

    def write(self, tick=None, corrupt=False):
        if corrupt:
            raw = b"\xff\xff\xff\xff"
        elif tick is None:
            raw = struct.pack("<I", 4)
        else:
            metadata = b"fixture\0" + struct.pack("<I", 1) + b"opaque\0" + struct.pack("<I", 8)
            raw = struct.pack("<I", 4 + len(metadata)) + metadata
            raw += struct.pack("<QI", tick, 4) + b"\xff\0\x80\x42" + bytes(4)
        assert kernel.WaitForSingleObject(self.mutex, 1000) == 0
        try:
            ctypes.memset(self.view, 0, 4096)
            ctypes.memmove(self.view, raw, len(raw))
        finally:
            kernel.ReleaseMutex(self.mutex)

    def close(self):
        kernel.UnmapViewOfFile(self.view)
        kernel.CloseHandle(self.mapping)
        kernel.CloseHandle(self.mutex)


def rows(path):
    if not path.exists():
        return []
    # WriteFile can race this diagnostic read; ignore only the unfinished final line.
    lines = path.read_bytes().splitlines(keepends=True)
    return [json.loads(line) for line in lines if line.endswith(b"\n")]


def await_row(path, predicate):
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        for row in rows(path):
            if predicate(row):
                return row
        time.sleep(0.05)
    raise AssertionError(f"Missing recorder transition: {rows(path)}")


def main():
    executable = str(Path(sys.argv[1]).resolve())
    channel = 3200000000 + os.getpid()
    source = None
    with tempfile.TemporaryDirectory(prefix="owh-recorder-") as temporary:
        path = Path(temporary) / "state.jsonl"
        stop = Path(temporary) / "STOP"
        process = subprocess.Popen([executable, "watch", "0", str(path), "--stop-file", str(stop)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            await_row(path, lambda r: r.get("format") == "owh-infodb-jsonl-1")
            source = Source(channel)
            initial = await_row(path, lambda r: r.get("source_channel") == channel and r.get("status") == 0)
            assert initial["fields"] == [{"category_hex": b"fixture".hex(), "key_hex": b"opaque".hex(),
                "producer_ticks": "1", "reserved_bytes": 8, "value_hex": "ff008042"}]
            source.write(2)
            await_row(path, lambda r: r.get("source_channel") == channel and
                      r.get("fields", [{}])[0].get("producer_ticks") == "2")
            source.write(corrupt=True)
            await_row(path, lambda r: r.get("source_channel") == channel and r.get("status") == 9)
            source.close(); source = None
            await_row(path, lambda r: r.get("source_channel") == channel and r.get("removed") is True)
            source = Source(channel); source.write()
            await_row(path, lambda r: r.get("source_channel") == channel and r.get("fields") == [])
            stop.touch()
            stdout, stderr = process.communicate(timeout=6)
            assert process.returncode == 0, (stdout, stderr)
            assert "stopped_filetime_utc" in rows(path)[-1]
            original = path.read_bytes()
            refused = subprocess.run([executable, "watch", "1", str(path)], capture_output=True, timeout=6)
            assert refused.returncode != 0 and path.read_bytes() == original
        finally:
            if process.poll() is None:
                process.kill(); process.communicate(timeout=5)
            if source:
                source.close()
    print("PASS indefinite CLI recording, late source, raw bytes, updates, corruption, removal, recreation, stop and overwrite refusal")


if __name__ == "__main__":
    main()
