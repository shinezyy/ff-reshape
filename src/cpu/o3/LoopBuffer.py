from m5.SimObject import SimObject
from m5.params import *

class LoopBuffer(SimObject):
    type = 'LoopBuffer'
    cxx_class = 'LoopBuffer'
    cxx_header = 'cpu/o3/loop_buffer.hh'

    numEntries = Param.Unsigned(64, "Number of entries")
    entrySize = Param.Unsigned(512, "Size of entries in bytes")
    enable = Param.Bool(False, "Enable")
    loopFiltering = Param.Bool(True, "Enable")
    maxForwardBranches = Param.Unsigned(12, "maxForwardBranches ")
