from m5.SimObject import SimObject
from m5.params import *
from m5.proxy import *

class IndirectPredictor(SimObject):
    type = 'IndirectPredictor'
    cxx_class = 'IndirectPredictor'
    cxx_header = "cpu/pred/itgt/indirect.hh"
    abstract = True

    numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")

class SimpleIndirectPredictor(IndirectPredictor):
    type = 'SimpleIndirectPredictor'
    cxx_class = 'SimpleIndirectPredictor'
    cxx_header = "cpu/pred/itgt/simple_indirect.hh"

    indirectHashGHR = Param.Bool(True, "Hash branch predictor GHR")
    indirectHashTargets = Param.Bool(True, "Hash path history targets")
    indirectSets = Param.Unsigned(256, "Cache sets for indirect predictor")
    indirectWays = Param.Unsigned(2, "Ways for indirect predictor")
    indirectTagSize = Param.Unsigned(16, "Indirect target cache tag bits")
    indirectPathLength = Param.Unsigned(3,
        "Previous indirect targets to use for path history")
    indirectGHRBits = Param.Unsigned(13, "Indirect GHR number of bits")
    instShiftAmt = Param.Unsigned(2, "Number of bits to shift instructions by")
