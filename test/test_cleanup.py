"""Discarding unfinished streams must stop their native worker threads."""

import subprocess
import sys

import pytest


@pytest.mark.parametrize("kind", ["compressobj", "decompressobj_blast", "decompressobj_pklib"])
def test_discard_unfinished_stream(kind, tmp_path):
    pytest.importorskip("psutil")
    # Isolate native crashes and count OS threads, which threading.enumerate misses.
    script = """
import ctypes
import gc
import sys
import time
import dclimplode
import psutil

if sys.platform == "win32":
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)

process = psutil.Process()
initial_threads = process.num_threads()

def discard_streams():
    for _ in range(16):
        stream = getattr(dclimplode, sys.argv[1])()
        if sys.argv[1] == "compressobj":
            stream.compress(b"unfinished input")
        else:
            stream.decompress(b"\\x00\\x06" + b"\\x00" * 32)
            assert not stream.eof
        del stream
    # PyPy does not destroy objects immediately when their reference count reaches zero.
    gc.collect()

def wait_for_thread_count_at_most(limit):
    deadline = time.monotonic() + 1
    while True:
        count = process.num_threads()
        if count <= limit or time.monotonic() >= deadline:
            return count
        time.sleep(0.01)

discard_streams()
threads_after_first_batch = wait_for_thread_count_at_most(initial_threads + 1)
# A second batch distinguishes a per-stream leak from one-time runtime thread setup.
discard_streams()
threads_after_second_batch = wait_for_thread_count_at_most(threads_after_first_batch)
counts = (initial_threads, threads_after_first_batch, threads_after_second_batch)
assert threads_after_first_batch <= initial_threads + 1, counts
assert threads_after_second_batch <= threads_after_first_batch, counts
"""
    subprocess.run(
        [sys.executable, "-c", script, kind],
        cwd=str(tmp_path),
        check=True,
        timeout=30,
    )


@pytest.mark.parametrize("case", ["compress", "flush", "decompressobj_blast", "decompressobj_pklib"])
def test_reject_finished_stream(case, tmp_path):
    # A subprocess contains regressions to a native access violation on Windows.
    script = """
import ctypes
import sys
import dclimplode

if sys.platform == "win32":
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)

if sys.argv[1] in ("compress", "flush"):
    stream = dclimplode.compressobj()
    stream.compress(b"hello")
    stream.flush()
    operation = lambda: stream.compress(b"again") if sys.argv[1] == "compress" else stream.flush()
else:
    compressor = dclimplode.compressobj()
    compressed = compressor.compress(b"hello") + compressor.flush()
    stream = getattr(dclimplode, sys.argv[1])()
    stream.decompress(compressed)
    if not stream.eof:
        stream.decompress(b"")
    assert stream.eof
    operation = lambda: stream.decompress(b"again")

try:
    operation()
except RuntimeError as error:
    assert "finalized" in str(error)
else:
    raise AssertionError("finished stream accepted more input")
"""
    subprocess.run(
        [sys.executable, "-c", script, case],
        cwd=str(tmp_path),
        check=True,
        timeout=30,
    )
