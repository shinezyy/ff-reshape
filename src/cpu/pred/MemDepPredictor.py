from m5.SimObject import SimObject
from m5.params import *

class MemDepPredictor(SimObject):
    type = 'MemDepPredictor'
    cxx_class = 'MemDepPredictor'
    cxx_header = 'cpu/pred/mem_dep_pred.hh'

    PCTableSize = Param.Unsigned(4096, "Size of PC indexed table")
    PCTableAssoc = Param.Unsigned(4, "Size of PC indexed table")
    PathTableSize = Param.Unsigned(4096, "Size of PC^PAth index table")
    PathTableAssoc = Param.Unsigned(4, "Size of PC^PAth index table")

    DistanceBits = Param.Unsigned(6, "Bit width of bits to represent store distance")
    ShamtBits = Param.Unsigned(3, "Bit width of bits to represent shit amount")
    StoreSizeBits = Param.Unsigned(2, "Bit width of bits to represent store size")
    ConfidenceBits = Param.Unsigned(10, "Bit width of bits to represent confidence")
    TagBits = Param.Unsigned(22, "Bit width of bits for tag")

    HistoryLen = Param.Unsigned(40, "Length of bits used for path history")
    BranchPathLen = Param.Unsigned(1, "Length of bits extract from branch")
    CallPathLen = Param.Unsigned(3, "Length of bits extract from call")

    TSSBFSize = Param.Unsigned(128, "Size of TSSBF table")
    TSSBFAssoc = Param.Unsigned(4, "Assoc of TSSBF index table")
    TSSBFTagBits = Param.Unsigned(38, "Bit width of bits for tag")

    SquashFactor = Param.Unsigned(3, "Importance of squashed miss prediction")
