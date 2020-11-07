# Copyright (c) 2012 The Regents of The University of Michigan
# Copyright (c) 2016 Centre National de la Recherche Scientifique
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
#
# Authors: Ron Dreslinski
#          Anastasiia Butko
#          Louisa Bessad

from m5.objects import *

#-----------------------------------------------------------------------
#                ex5 big core (based on the ARM Cortex-A15)
#-----------------------------------------------------------------------

# Simple ALU Instructions have a latency of 1
class xiangshan_IntALU(FUDesc):
    opList = [ OpDesc(opClass='IntAlu', opLat=1) ]
    count = 5

class xiangshan_IntMult(FUDesc):
    opList = [ OpDesc(opClass='IntMult', opLat=3, pipelined=True) ]
    count = 2

class xiangshan_IntDiv(FUDesc):
    opList = [ OpDesc(opClass='IntDiv', opLat=15, pipelined=False)]
    count = 2

class xiangshan_Ipr(FUDesc):
    opList = [ OpDesc(opClass='IprAccess', opLat=3, pipelined=True) ]
    count = 1

# Floating point and SIMD instructions
class xiangshan_FpFMA(FUDesc):
    opList = [
            OpDesc(opClass='FloatAdd', opLat=4, pipelined=True),
            OpDesc(opClass='FloatMult', opLat=4, pipelined=True),
            OpDesc(opClass='FloatMultAcc', opLat=4, pipelined=True),
            ]
    count = 3

class xiangshan_FpAdder(FUDesc):
    opList = [
            OpDesc(opClass='FloatAdd', opLat=3, pipelined=True),
            ]
    count = 2

class xiangshan_FpCmp(FUDesc):
    opList = [
            OpDesc(opClass='FloatCmp', opLat=2, pipelined=True),
            ]
    count = 2

class xiangshan_FpCvt(FUDesc):
    opList = [
            OpDesc(opClass='FloatCvt', opLat=2, pipelined=True),
            ]
    count = 2

class xiangshan_FpDiv(FUDesc):
    opList = [
            OpDesc(opClass='FloatDiv', opLat=12, pipelined=False),
            OpDesc(opClass='FloatSqrt', opLat=12, pipelined=False),
            ]
    count = 2

# Load/Store Units
class xiangshan_Load(FUDesc):
    opList = [ OpDesc(opClass='MemRead', opLat=2, pipelined=True) ]
    count = 2

class xiangshan_Store(FUDesc):
    opList = [OpDesc(opClass='MemWrite', opLat=2, pipelined=True) ]
    count = 2

# Functional Units for this CPU
class xiangshan_FUP(FUPool):
    FUList = [
            xiangshan_IntALU(), xiangshan_IntMult(), xiangshan_IntDiv(), xiangshan_Ipr(),
            xiangshan_FpAdder(),
            xiangshan_FpFMA(), xiangshan_FpCmp(), xiangshan_FpCvt(), xiangshan_FpDiv(),
            xiangshan_Load(), xiangshan_Store(),
            ]

class XiangshanCore(DerivO3CPU):
    LQEntries = 44
    SQEntries = 32
    LSQDepCheckShift = 0
    LFSTSize = 1024
    SSITSize = 1024
    decodeToFetchDelay = 1
    renameToFetchDelay = 1
    iewToFetchDelay = 1
    commitToFetchDelay = 1
    renameToDecodeDelay = 1
    iewToDecodeDelay = 1
    commitToDecodeDelay = 1
    iewToRenameDelay = 1
    commitToRenameDelay = 1
    commitToIEWDelay = 1
    fetchWidth = 8
    fetchBufferSize = 64
    fetchToDecodeDelay = 4
    decodeWidth = 6
    decodeToRenameDelay = 1
    renameWidth = 6
    renameToIEWDelay = 2
    issueToExecuteDelay = 1
    dispatchWidth = 6
    issueWidth = 10
    wbWidth = 8
    fuPool = xiangshan_FUP()
    iewToCommitDelay = 1
    renameToROBDelay = 1
    commitWidth = 6
    squashWidth = 6
    trapLatency = 13
    backComSize = 10
    forwardComSize = 10
    numPhysIntRegs = 128
    numPhysFloatRegs = 128
    numIQEntries = 56
    numROBEntries = 128

    switched_out = False
    branchPred = LTAGE()
    loopBuffer = LoopBuffer()

class L1Cache(Cache):
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 16
    writeback_clean = True

class L1_ICache(L1Cache):
    size = '16kB'
    assoc = 4
    is_read_only = True
    # Writeback clean lines as well
    writeback_clean = False

class L1_ICachePlus(L1Cache):
    size = '128kB'
    assoc = 8
    is_read_only = False

    # Dont touch L2
    writeback_clean = False

    tag_latency = 3
    data_latency = 3
    response_latency = 3
    clusivity='mostly_incl'

class L1_DCache(L1Cache):
    size = '32kB'
    assoc = 8
    write_buffers = 16

class L2Cache(Cache):
    writeback_clean = True
    assoc = 8
    tag_latency = 12
    data_latency = 12
    response_latency = 5
    mshrs = 32
    tgts_per_mshr = 8
    write_buffers = 8
    size = '512kB'
    clusivity='mostly_excl'

    prefetcher = BOPPrefetcher()

class L3Cache(Cache):
    size = '4MB'
    assoc = 8
    tag_latency = 40
    data_latency = 40
    response_latency = 40
    mshrs = 20
    tgts_per_mshr = 12
    write_buffers = 8
    clusivity='mostly_excl'
