from m5.SimObject import SimObject
from m5.params import *

class LoopBuffer(SimObject):
    type = 'LoopBuffer'
    cxx_class = 'LoopBuffer'
    cxx_header = 'cpu/o3/loop_buffer.hh'

    numEntries = Param.Unsigned(64, "Number of entries")
    entrySize = Param.Unsigned(64, "Size of entries in bytes")
    enable = Param.Bool(True, "Enable")
