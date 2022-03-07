# thin wrapper as pybind11 does not support overwriting attribute

from .dclimplode import *
decompressobj = decompressobj_blast

# decompressobj_pklib requires flushing decompressor, hence incompatible with zipfile39.
