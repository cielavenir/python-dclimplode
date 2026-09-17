import os
import dclimplode
import io
import pytest


@pytest.mark.parametrize('type',[-1,2])
def test_reject_invalid_compression_type(type):
    with pytest.raises(ValueError, match="invalid type"):
        dclimplode.compressobj(type=type)


@pytest.mark.parametrize('type',[0,1])
@pytest.mark.parametrize('dictsize',[1024,2048,4096])
@pytest.mark.parametrize('decompressobj',[dclimplode.decompressobj_blast,dclimplode.decompressobj_pklib])
def test_empty_round_trip(type, dictsize, decompressobj):
    compressor = dclimplode.compressobj(type=type, dictsize=dictsize)
    assert compressor.compress(b"") == b""
    compressed = compressor.flush()

    decompressor = decompressobj()
    output = decompressor.decompress(compressed)
    if not decompressor.eof:
        output += decompressor.decompress(b"")

    assert decompressor.eof
    assert output == b""

@pytest.mark.parametrize('type',[0,1])
@pytest.mark.parametrize('decompressobj',[dclimplode.decompressobj_blast,dclimplode.decompressobj_pklib])
def test_dclimplode(type, decompressobj):
    bytesio = io.BytesIO()
    with open(os.path.join(os.path.dirname(__file__), '10000SalesRecords.csv'), 'rb') as f:
        content = f.read()
        f.seek(0)
        l = len(content)
        siz = 1024
        cnt = (l+siz-1)//siz
        dfl = dclimplode.compressobj(type=type)
        for i in range(cnt):
            bytesio.write(dfl.compress(f.read(siz)))
        bytesio.write(dfl.flush())
        # print(len(bytesio.getvalue()))
    bytesio.seek(0)
    ifl = decompressobj()
    decompressed_content = ifl.decompress(bytesio.read())
    if not ifl.eof:
        decompressed_content += ifl.decompress(b'')
    assert decompressed_content == content


@pytest.mark.parametrize('type',[0,1])
@pytest.mark.parametrize('decompressobj',[dclimplode.decompressobj_blast,dclimplode.decompressobj_pklib])
def test_chunked_decompression(type, decompressobj):
    content = b"0123456789abcdef" * 1024
    compressor = dclimplode.compressobj(type=type)
    compressed = compressor.compress(content) + compressor.flush()
    decompressor = decompressobj()
    output = b""
    sizes = (1, 2, 3, 5, 8, 13)
    offset = 0
    index = 0
    while offset < len(compressed):
        size = sizes[index % len(sizes)]
        output += decompressor.decompress(compressed[offset:offset + size])
        offset += size
        index += 1
    if not decompressor.eof:
        output += decompressor.decompress(b"")
    assert decompressor.eof
    assert output == content


def test_pklib_rejects_truncated_input():
    decompressor = dclimplode.decompressobj_pklib()
    assert decompressor.decompress(b"\x00") == b""
    with pytest.raises(RuntimeError, match="explode\\(\\) error"):
        decompressor.decompress(b"")
    assert not decompressor.eof


@pytest.mark.parametrize('decompressobj',[dclimplode.decompressobj_blast,dclimplode.decompressobj_pklib])
def test_rejects_backreference_before_output(decompressobj):
    decompressor = decompressobj()
    with pytest.raises(RuntimeError):
        decompressor.decompress(bytes.fromhex("00063b01ff"))
    assert not decompressor.eof


def _boundary_match_stream():
    bits = []

    def add(value, count):
        bits.extend((value >> index) & 1 for index in range(count))

    for _ in range(4095):
        add(0, 1)
        add(ord("A"), 8)

    # A 518-byte distance-one match beginning at output offset 4095.
    add(1, 1)
    add(0, 7)
    add(254, 8)
    add(3, 2)
    add(0, 6)

    # End marker.
    add(1, 1)
    add(0, 7)
    add(255, 8)

    body = bytearray((len(bits) + 7) // 8)
    for index, bit in enumerate(bits):
        body[index // 8] |= bit << (index % 8)
    return bytes((dclimplode.CMP_BINARY, 6)) + bytes(body)


@pytest.mark.parametrize('decompressobj',[dclimplode.decompressobj_blast,dclimplode.decompressobj_pklib])
def test_longest_match_at_output_boundary(decompressobj):
    decompressor = decompressobj()
    output = decompressor.decompress(_boundary_match_stream())
    if not decompressor.eof:
        output += decompressor.decompress(b"")
    assert decompressor.eof
    assert output == b"A" * (4095 + 518)
