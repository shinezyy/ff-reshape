from m5.SimObject import SimObject
from m5.params import *
from m5.proxy import *

class DirectTargetPredictor(SimObject):
    type = 'DirectTargetPredictor'
    cxx_class = 'DirectTargetPredictor'
    cxx_header = "cpu/pred/tgt/direct_target_pred.hh"
    abstract = True

    numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")
    instShiftAmt = Param.Unsigned(2, "Number of bits to shift instructions by")

class DefaultBTB(DirectTargetPredictor):
    type = 'DefaultBTB'
    cxx_class = 'DefaultBTB'
    cxx_header = "cpu/pred/tgt/btb.hh"

    numEntries = Param.Unsigned(4096, "Number of BTB entries")
    tagBits = Param.Unsigned(16, "Size of the BTB tags, in bits")
    numWays    = Param.Unsigned(1, "Number of BTB ways")
