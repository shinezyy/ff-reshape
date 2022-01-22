from m5.params import *
from m5.objects.Probe import ProbeListenerObject

class DepCheck(ProbeListenerObject):
    """Probe for collecting inter-/intra-group dependencies."""

    type = 'DepCheck'
    cxx_header = "cpu/simple/probes/depcheck.hh"
    cxx_class = 'gem5::DepCheck'

    groupSize = Param.UInt32(64, "Group Size (insts)")
