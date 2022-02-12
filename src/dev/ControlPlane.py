from m5.params import *
from m5.proxy import *

from m5.objects.Device import BasicPioDevice

from m5.objects.O3CPU import DerivO3CPU
from m5.objects.Cache import Cache

class ControlPlane(BasicPioDevice):
    type = 'ControlPlane'
    cxx_header = "dev/controlplane.hh"

    # cxx_exports = [
    #     PyBindMethod("switchOut"),
    #     PyBindMethod("takeOverFrom"),
    #     PyBindMethod("getTaskId"),
    #     PyBindMethod("setTaskId"),
    # ]

    pio_addr = 0x20000
    pio_size = Param.Addr(0x10000, "cp space size")

    cpus = VectorParam.DerivO3CPU([],'cpus under control')

    l2s = VectorParam.Cache([],'l2 caches under control')
    l3 = Param.Cache(NULL,'l3 cache under control')
