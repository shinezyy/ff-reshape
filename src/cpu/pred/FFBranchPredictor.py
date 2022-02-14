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
