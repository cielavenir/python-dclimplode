[![PyPI](https://img.shields.io/pypi/v/dclimplode)](https://pypi.org/project/dclimplode/)

## dclimplode

a (quick) binding for https://github.com/madler/zlib/blob/master/contrib/blast/blast.c and https://github.com/ladislav-zezula/StormLib/blob/master/src/pklib/implode.c

DCL stands for `PKWARE(R) Data Compression Library`.

```
o = dclimplode.compressobj()
s = o.compress(b'hello')+o.flush()
o = dclimplode.decompressobj()
o.decompress(s) == b'hello'
```

## tested versions

- Python 2.7
- Python 3.9
- PyPy [2.7] 7.3.3
- PyPy [3.7] 7.3.5
    - For PyPy2, pip needs to be 20.1.x cf https://github.com/pypa/pip/issues/8653
    - PyPy needs to be 7.3.1+ cf https://github.com/pybind/pybind11/issues/2436
- Pyston [3.8] 2.3

## special thanks

- https://github.com/JoshVarga/blast showed dclimplode compression by Ladislav Zezula (I knew dclimplode decompression in zlib for a long time though)
- unlike [deflate64 infback9](https://github.com/brianhelba/zipfile-deflate64/pull/18), making dclimplode blast resumable does not look possible (for me). instead I used threaded decoder. basic idea is from https://github.com/miurahr/pyppmd/pull/33#issuecomment-894676975 ('s linked commit f224a04).

