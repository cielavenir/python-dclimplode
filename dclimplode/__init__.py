# thin wrapper as pybind11 does not support overwriting attribute

from .version import __version__
from .dclimplode import compressobj, decompressobj_pklib, decompressobj_blast
from .dclimplode import CMP_BINARY, CMP_ASCII
decompressobj = decompressobj_blast

# decompressobj_pklib requires flushing decompressor, hence incompatible with zipfile39.
