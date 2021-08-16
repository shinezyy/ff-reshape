from __future__ import print_function

from m5.defines import buildEnv
from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject
from m5.objects.BaseCPU import BaseCPU
from m5.objects.DummyChecker import DummyChecker
from m5.objects.BranchPredictor import *
from m5.objects.TimingExpr import TimingExpr

from m5.objects.FuncUnit import OpClass

class NemuCPU(BaseCPU):
    type = 'NemuCPU'
    cxx_header = "cpu/nemu/cpu.hh"

    @classmethod
    def memory_mode(cls):
        return 'timing'

    @classmethod
    def require_caches(cls):
        return True

    @classmethod
    def support_take_over(cls):
        return False

    branchPred = Param.BranchPredictor(TournamentBP(
        numThreads = Parent.numThreads), "Branch Predictor")

    def addCheckerCpu(self):
        print("Checker not yet supported by NemuCPU")
        exit(1)

    gcpt_file = Param.String(Parent.gcpt_file, "Input Generic Checkpoint image file or BBL")
    nemu_backed_store = Param.String("./backed_mem_for_cpt", "Output GCPT path")
