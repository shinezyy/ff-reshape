# Copyright (c) 2016, 2019 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2005-2007 The Regents of The University of Michigan
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
# Authors: Kevin Lim

from __future__ import print_function

from m5.defines import buildEnv
from m5.params import *
from m5.proxy import *
from m5.objects.BaseCPU import BaseCPU
from m5.objects.FFFUPool import *
from m5.objects.FFChecker import FFChecker
from m5.objects.BranchPredictor import *
from m5.objects.MemDepPredictor import *
from m5.objects.LoopBuffer import *
from m5.objects.O3CPU import SMTQueuePolicy, CommitPolicy
from m5.objects.O3CPU import SMTFetchPolicy

coreWidth = 4

class DerivFFCPU(BaseCPU):
    type = 'DerivFFCPU'
    cxx_header = 'cpu/forwardflow/deriv.hh'

    @classmethod
    def memory_mode(cls):
        return 'timing'

    @classmethod
    def require_caches(cls):
        return True

    @classmethod
    def support_take_over(cls):
        return True

    activity = Param.Unsigned(0, "Initial count")

    cacheStorePorts = Param.Unsigned(200, "Cache Ports. "
          "Constrains stores only.")
    cacheLoadPorts = Param.Unsigned(200, "Cache Ports. "
          "Constrains loads only.")

    decodeToFetchDelay = Param.Cycles(1, "Decode to fetch delay")
    renameToFetchDelay = Param.Cycles(1 ,"Rename to fetch delay")
    iewToFetchDelay = Param.Cycles(1, "Issue/Execute/Writeback to fetch "
                                   "delay")
    commitToFetchDelay = Param.Cycles(1, "Commit to fetch delay")
    fetchWidth = Param.Unsigned(coreWidth, "Fetch width")
    fetchBufferSize = Param.Unsigned(64, "Fetch buffer size in bytes")
    fetchQueueSize = Param.Unsigned(32, "Fetch queue size in micro-ops "
                                    "per-thread")

    renameToDecodeDelay = Param.Cycles(1, "Rename to decode delay")
    iewToDecodeDelay = Param.Cycles(1, "Issue/Execute/Writeback to decode "
                                    "delay")
    commitToDecodeDelay = Param.Cycles(1, "Commit to decode delay")
    fetchToDecodeDelay = Param.Cycles(1, "Fetch to decode delay")
    decodeWidth = Param.Unsigned(coreWidth, "Decode width")

    iewToRenameDelay = Param.Cycles(1, "Issue/Execute/Writeback to rename "
                                    "delay")
    commitToRenameDelay = Param.Cycles(1, "Commit to rename delay")
    decodeToRenameDelay = Param.Cycles(1, "Decode to rename delay")
    allocationWidth = Param.Unsigned(coreWidth, "Rename width")

    commitToIEWDelay = Param.Cycles(1, "Commit to "
               "Issue/Execute/Writeback delay")
    renameToIEWDelay = Param.Cycles(2, "Rename to "
               "Issue/Execute/Writeback delay")

    allocationToDIEWCDelay = Param.Cycles(1, "Allocation to "
               "Issue/Execute/Writeback delay")

    issueToExecuteDelay = Param.Cycles(1, "Issue to execute delay (internal "
              "to the IEW stage)")
    dispatchWidth = Param.Unsigned(coreWidth, "Dispatch width")
    issueWidth = Param.Unsigned(coreWidth, "Issue width")
    wbWidth = Param.Unsigned(coreWidth, "Writeback width")
    fuPool = Param.FFFUPool(GroupedFUPool(), "Functional Unit pool")

    iewToCommitDelay = Param.Cycles(1, "Issue/Execute/Writeback to commit "
               "delay")
    renameToROBDelay = Param.Cycles(1, "Rename to reorder buffer delay")
    commitWidth = Param.Unsigned(coreWidth, "Commit width")
    squashWidth = Param.Unsigned(coreWidth, "Squash width")
    trapLatency = Param.Cycles(13, "Trap latency")
    fetchTrapLatency = Param.Cycles(1, "Fetch trap latency")

    backComSize = Param.Unsigned(10,
            "Time buffer size for backwards communication")
    forwardComSize = Param.Unsigned(10,
            "Time buffer size for forward communication")

    LQEntries = Param.Unsigned(72, "Number of load queue entries")
    SQEntries = Param.Unsigned(56, "Number of store queue entries")
    LSQDepCheckShift = Param.Unsigned(4,
            "Number of places to shift addr before check")
    LSQCheckLoads = Param.Bool(True,
        "Should dependency violations be checked"
        " for loads & stores or just stores")
    store_set_clear_period = Param.Unsigned(250000,
            "Number of load/store insts before"
            " the dep predictor should be invalidated")
    LFSTSize = Param.Unsigned(1024, "Last fetched store table size")
    SSITSize = Param.Unsigned(1024, "Store set ID table size")

    numRobs = Param.Unsigned(1, "Number of Reorder Buffers");

    numPhysIntRegs = Param.Unsigned(256, "Number of physical integer registers")
    numPhysFloatRegs = Param.Unsigned(256, "Number of physical floating point "
                                      "registers")
    # most ISAs don't use condition-code regs, so default is 0
    _defaultNumPhysCCRegs = 0
    if buildEnv['TARGET_ISA'] in ('arm','x86'):
        # For x86, each CC reg is used to hold only a subset of the
        # flags, so we need 4-5 times the number of CC regs as
        # physical integer regs to be sure we don't run out.  In
        # typical real machines, CC regs are not explicitly renamed
        # (it's a side effect of int reg renaming), so they should
        # never be the bottleneck here.
        _defaultNumPhysCCRegs = Self.numPhysIntRegs * 5
    numPhysVecRegs = Param.Unsigned(256, "Number of physical vector "
                                      "registers")
    numPhysCCRegs = Param.Unsigned(_defaultNumPhysCCRegs,
                                   "Number of physical cc registers")
    numIQEntries = Param.Unsigned(96, "Number of instruction queue entries")
    numROBEntries = Param.Unsigned(224, "Number of reorder buffer entries")

    smtNumFetchingThreads = Param.Unsigned(1, "SMT Number of Fetching Threads")
    smtFetchPolicy = Param.SMTFetchPolicy('RoundRobin', "SMT Fetch policy")
    smtLSQPolicy    = Param.SMTQueuePolicy('Partitioned',
            "SMT LSQ Sharing Policy")
    smtLSQThreshold = Param.Int(100, "SMT LSQ Threshold Sharing Parameter")
    smtIQPolicy    = Param.String('Partitioned', "SMT IQ Sharing Policy")
    smtIQThreshold = Param.Int(100, "SMT IQ Threshold Sharing Parameter")
    smtROBPolicy   = Param.String('Partitioned', "SMT ROB Sharing Policy")
    smtROBThreshold = Param.Int(100, "SMT ROB Threshold Sharing Parameter")
    smtCommitPolicy = Param.String('RoundRobin', "SMT Commit Policy")

    #branchPred = Param.BranchPredictor(TournamentBP(numThreads =
    #                                                   Parent.numThreads),
    branchPred = Param.BranchPredictor(LTAGE(numThreads =
                                                       Parent.numThreads),
                                       "Branch Predictor")

    mDepPred = Param.MemDepPredictor(MemDepPredictor(), "memory dep predictor")

    loopBuffer = Param.LoopBuffer(LoopBuffer(), "Loopo Buffer")
    needsTSO = Param.Bool(buildEnv['TARGET_ISA'] == 'x86',
                          "Enable TSO Memory model")

    def addCheckerCpu(self):
        print("ERROR: Checker not supported for Omegaflow")
        exit(1)

    DQDepth = Param.Unsigned(48, "depth of each dataflow queue bank")
    numOperands = Param.Unsigned(4,
            "number of dest and src operands for each instruction")
    numDQBanks = Param.Unsigned(4, "number of DQ banks per group")
    numDQGroups = Param.Unsigned(8, "number of DQ groups")
    MGCenterLatency = Param.Bool(True, "One cycle latency for mem acc when using multiple groups")

    interGroupBW = Param.Unsigned(16, "interGroupBW")

    pendingQueueDepth = Param.Unsigned(16, "max depth of pending wakeup pointers")

    pendingWakeupQueueDepth = Param.Unsigned(16, "depth of pending wakeup pointers")
    pendingFwPointerQueueDepth = Param.Unsigned(16, "depth of pending forward pointers")

    PendingWakeupThreshold = Param.Unsigned(16*6, "PendingWakeupThreshold ")
    PendingWakeupMaxThreshold = Param.Unsigned(16, "PendingWakeupMaxThreshold ")
    PendingFwPointerThreshold = Param.Unsigned(16*6, "PendingFwPointerThreshold ")

    MaxCheckpoints = Param.Unsigned(16, "Max pending branch instructions")

    ForwardThreshold = Param.Unsigned(3, "LargeFanoutThreshold")

    readyQueueSize = Param.Unsigned(6, "readyQueueSize")

    EnableReshape = Param.Bool(False, "EnableReshape")

    DecoupleOpPosition = Param.Bool(False, "DecoupleOpPosition")

    ReadyHint = Param.Bool(False, "ReadyHint")

    TermMax = Param.Unsigned(32, "TermMax")

    MINWakeup = Param.Bool(False, "MINWakeup")
    XBarWakeup = Param.Bool(False, "XBarWakeup")
    NarrowXBarWakeup = Param.Bool(False, "NarrowXBarWakeup")
    DediXBarWakeup = Param.Bool(False, "DediXBarWakeup")

    AgedWakeQueuePush = Param.Bool(False, "AgedWakeQueuePush")
    AgedWakeQueuePktSel = Param.Bool(False, "AgedWakeQueuePktSel")

    MaxReadyQueueSize = Param.Unsigned(4, "MaxReadyQueueSize")

    NarrowLocalForward = Param.Bool(False, 'NarrowLocalForward')

    MemInstAllowedAfterFence = Param.Unsigned(6, "MemInstAllowedAfterFence ")
