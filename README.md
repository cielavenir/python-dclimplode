[![PyPI](https://img.shields.io/pypi/v/dclimplode)](https://pypi.org/project/dclimplode/)

## dclimplode

a (light) binding for https://github.com/madler/zlib/blob/master/contrib/blast/blast.c and https://github.com/ladislav-zezula/StormLib/blob/master/src/pklib/implode.c

DCL stands for `PKWARE(R) Data Compression Library`.

```
o = dclimplode.compressobj()
s = o.compress(b'hello')+o.flush()
o = dclimplode.decompressobj()
o.decompress(s) == b'hello'
```

## Fork build fix

This fork uses setuptools' standard MSVC compiler on Windows, removing the
legacy override that failed with setuptools 81 and newer (`dry_run` missing).
Discarding an unfinished stream also waits for its worker to exit before freeing
its buffers, preventing leaked threads and use-after-free crashes on Windows.
The compression API and Unix compiler customization are unchanged.

Build with Python 3.14 and uv using `uv build`. Windows requires Visual Studio
Build Tools with the Desktop development with C++ workload and a Windows SDK.
The Build check workflow builds an sdist and wheel, installs the wheel, and runs
the compression/decompression tests on Windows, macOS and Linux.

## tested versions

- CPython 2.7 and 3.5 through 3.14
- PyPy 2.7, 3.6, 3.7, 3.10, and 3.11
    - Legacy PyPy wheels are built on Linux, plus PyPy 3.6 on Windows.
    - PyPy 3.10 and 3.11 wheels are built on Linux and Windows; PyPy 3.11
      wheels are also built for Intel and Apple Silicon macOS.
    - For PyPy2, pip needs to be 20.1.x cf https://github.com/pypa/pip/issues/8653
    - PyPy needs to be 7.3.1+ cf https://github.com/pybind/pybind11/issues/2436
- Pyston [3.8] 2.3.5 on Linux x86-64

## special thanks

- https://github.com/JoshVarga/blast showed dclimplode compression by Ladislav Zezula (I knew dclimplode decompression in zlib for a long time though)
- unlike [deflate64 infback9](https://github.com/brianhelba/zipfile-deflate64/pull/18), making dclimplode blast resumable does not look possible (for me). instead I used threaded decoder. basic idea is from https://github.com/miurahr/pyppmd/pull/33#issuecomment-894676975 ('s linked commit f224a04).
