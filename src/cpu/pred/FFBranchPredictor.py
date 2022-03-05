from m5.SimObject import SimObject
from m5.params import *
from m5.proxy import *

class FFBranchPredictor(SimObject):
    type = 'FFBranchPredictor'
    cxx_class = 'FFBPredUnit'
    cxx_header = "cpu/pred/ff_bpred_unit.hh"
    abstract = True

    numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")
    numLookAhead = Param.Unsigned(64, "Number of look-ahead insts")

class FFOracleBP(FFBranchPredictor):
    type = 'FFOracleBP'
    cxx_class = 'FFOracleBP'
    cxx_header = "cpu/pred/ff_oracle.hh"

    presetAccuracy = Param.Float(1.0, "Preset accuracy")
    randNumSeed = Param.Unsigned(0, "Random number seed")

class ForwardN(FFBranchPredictor):
    type = 'ForwardN'
    cxx_class = 'ForwardN'
    cxx_header = "cpu/pred/forward_n.hh"

    histLength = Param.Unsigned(8, "History length for control inst PCs")

    traceStart = Param.Unsigned(0, "Trace misprediction from inst count")
    traceCount = Param.Unsigned(0, "Trace misprediction count")

    numGTabBanks = Param.Unsigned(4, "Number of global table banks")
    numGTabEntries = Param.Unsigned(1024, "Number of global table entries")
    numBTabEntries = Param.Unsigned(4096, "Number of base table entries")
    histLenInitial = Param.Unsigned(10, "Initial history length series")
    histLenGrowth = Param.Float(2, "Growth factor for history length series")

    randNumSeed = Param.Unsigned(0, "Random number seed")

