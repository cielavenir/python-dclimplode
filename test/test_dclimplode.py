import os
import dclimplode
import io
import pytest

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
