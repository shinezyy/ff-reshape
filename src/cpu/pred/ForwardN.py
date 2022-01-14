from m5.SimObject import SimObject
from m5.params import *


class ForwardN(SimObject):
    type = 'ForwardN'
    cxx_class = 'gem5::branch_prediction::ForwardN'
    cxx_header = "cpu/pred/forward_n.hh"

    histLength = Param.Unsigned(1, "History length for control inst PCs")
    histTakenLength = \
        Param.Unsigned(1, "History length for control inst takens")

    traceStart = Param.Unsigned(0, "Trace misprediction from inst count")
    traceCount = Param.Unsigned(0, "Trace misprediction count")
