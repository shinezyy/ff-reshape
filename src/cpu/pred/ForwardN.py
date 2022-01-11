from m5.SimObject import SimObject
from m5.params import *


class ForwardN(SimObject):
    type = 'ForwardN'
    cxx_class = 'gem5::branch_prediction::ForwardN'
    cxx_header = "cpu/pred/forward_n.hh"

    traceStart = Param.Unsigned(0, "Trace misprediction from inst count")
    traceCount = Param.Unsigned(0, "Trace misprediction count")
