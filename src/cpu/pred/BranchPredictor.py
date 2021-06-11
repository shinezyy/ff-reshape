# Copyright (c) 2012 Mark D. Hill and David A. Wood
# Copyright (c) 2015 The University of Wisconsin
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.SimObject import SimObject
from m5.params import *
from m5.proxy import *

from m5.objects.DirectionPredictor import *
from m5.objects.DirectTargetPredictor import *
from m5.objects.IndirectTargetPredictor import *


class BranchPredictor(SimObject):
    type = 'BranchPredictor'
    cxx_class = 'BPredUnit'
    cxx_header = "cpu/pred/bpred_unit.hh"
    abstract = True

    numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")

    RASSize = Param.Unsigned(16, "RAS size")
    instShiftAmt = Param.Unsigned(2, "Number of bits to shift instructions by")

    # indirectBranchPred = Param.IndirectPredictor(SimpleIndirectPredictor(),
    #   "Indirect branch predictor, set to NULL to disable indirect predictions")

    directionPred = Param.DirectionPredictor(NULL, "Conditional branch predictor")
    directTargetPred = Param.DirectTargetPredictor(DefaultBTB(), "Direct target predictor")
    indirectTargetPred = Param.IndirectPredictor(SimpleIndirectPredictor(), "Indirect target predictor")

    useInstInfo = Param.Bool(True, "Whether use inst info to predict")

# class BranchPredictorNoPredecode(SimObject):
#     type = 'BranchPredictorNoPredecode'
#     cxx_class = 'BPredUnitNoPredecode'
#     cxx_header = "cpu/pred/bpred_unit_npd.hh"
#     abstract = True

#     numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")
#     BTBEntries = Param.Unsigned(4096, "Number of BTB entries")
#     BTBTagSize = Param.Unsigned(16, "Size of the BTB tags, in bits")
#     BTBWays    = Param.Unsigned(1, "Number of BTB ways")
#     instShiftAmt = Param.Unsigned(2, "Number of bits to shift instructions by")

#     # indirectBranchPred = Param.IndirectPredictor(SimpleIndirectPredictor(),
#     #   "Indirect branch predictor, set to NULL to disable indirect predictions")

#     indirectBranchPred = Param.IndirectPredictor(NULL,
#       "Indirect branch predictor, set to NULL to disable indirect predictions")

# class BPU(SimObject):
#     type = 'BPU'
#     cxx_class = 'BPU'
#     cxx_header = "cpu/pred/bpu.hh"
#     abstract = True

#     numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")
#     instShiftAmt = Param.Unsigned(2, "Number of bits to shift instructions by")

#     directionPred = Param.DirectionPredictor(NULL, "Conditional branch predictor")
#     directTargetPred = Param.DirectTargetPredictor(NULL, "Direct target predictor")
#     indirectTargetPred = Param.IndirectTargetPredictor(NULL, "Indirect target predictor")


# class DirectTargetPredictor(SimObject):
#     type = 'DirectTargetPredictor'
#     cxx_class = 'DirectTargetPredictor'
#     cxx_header = "cpu/pred/direct_target_pred.hh"
#     abstract = True

#     numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")

# class IndirectTargetPredictor(SimObject):
#     type = 'IndirectTargetPredictor'
#     cxx_class = 'IndirectTargetPredictor'
#     cxx_header = "cpu/pred/indirect_target_pred.hh"
#     abstract = True

#     numThreads = Param.Unsigned(Parent.numThreads, "Number of threads")

# class Stage1BP(LocalBP):
#     BTBEntries = Param.Unsigned(256, "Number of BTB entries")
#     BTBTagSize = Param.Unsigned(20, "Size of the BTB tags, in bits")
#     localPredictorSize = Param.Unsigned(256, "Size of local predictor")
#     useInstInfo = Param.Bool(False, "Whether use inst info to predict")

# class Stage2BP(LocalBP):
#     BTBEntries = Param.Unsigned(2048, "Number of BTB entries")
#     BTBTagSize = Param.Unsigned(24, "Size of the BTB tags, in bits")
#     localPredictorSize = Param.Unsigned(4096, "Size of local predictor")
#     useInstInfo = Param.Bool(False, "Whether use inst info to predict")

class XSBP(BranchPredictor):
    type = 'XSBP'
    cxx_class = 'XSBP'
    cxx_header = "cpu/pred/xsbp.hh"
    directionPred = Param.DirectionPredictor(LTAGE(), "")
    # indirectTargetPred = Param.IndirectPredictor(NULL, "")
