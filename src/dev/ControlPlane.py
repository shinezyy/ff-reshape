from m5.params import *
from m5.proxy import *
from m5.SimObject import *

from m5.objects.Device import BasicPioDevice

from m5.objects.O3CPU import DerivO3CPU
from m5.objects.Cache import Cache

class ControlPlane(BasicPioDevice):
    type = 'ControlPlane'
    cxx_header = "dev/controlplane.hh"

    cxx_exports = [
        PyBindMethod("startTraining"),
        PyBindMethod("startQoS"),
        PyBindMethod("startTTI"),
        PyBindMethod("endTTI"),
        PyBindMethod("setJob"),
        PyBindMethod("tuning"),
    ]

    pio_addr = 0x20000
    pio_size = Param.Addr(0x10000, "cp space size")

    cpus = VectorParam.DerivO3CPU([],'cpus under control')

    l2s = VectorParam.Cache([],'l2 caches under control')
    l3 = Param.Cache(NULL,'l3 cache under control')
    l2inc = Param.UInt32(10000, 'l2 tb inc')
    l3inc = Param.UInt32(10000, 'l3 tb inc')
    l2_tb_size = Param.UInt32(1000, 'l3 tb size')
    l3_tb_size = Param.UInt32(1000, 'l3 tb size')

    system = Param.System(Parent.any, "System that the control plane belongs to.")

    l2_waymask_set = VectorParam.UInt64([], 'l2_waymask_choose')
    l3_waymask_set = VectorParam.UInt64([], 'l3_waymask_choose')