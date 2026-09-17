"""Installed-wheel smoke test compatible with every supported Python version."""

from __future__ import print_function

import dclimplode


def round_trip(compression_type, decompressor_type):
    """Round-trip data through one compressor/decompressor pair."""
    content = b"dclimplode legacy wheel smoke test" * 64
    compressor = dclimplode.compressobj(type=compression_type)
    compressed = compressor.compress(content) + compressor.flush()
    decompressor = decompressor_type()
    output = decompressor.decompress(compressed)
    if not decompressor.eof:
        output += decompressor.decompress(b"")
    assert decompressor.eof
    assert output == content


for compression_type in (dclimplode.CMP_BINARY, dclimplode.CMP_ASCII):
    round_trip(compression_type, dclimplode.decompressobj_blast)
    round_trip(compression_type, dclimplode.decompressobj_pklib)
