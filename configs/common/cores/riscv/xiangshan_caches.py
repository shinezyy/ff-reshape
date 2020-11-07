from __future__ import print_function

import m5
from m5.objects import *
from common.cores.riscv.xiangshan import *


def XiangshanConfigCache(options, system):
    system.l2 = L2Cache(clk_domain=system.cpu_clk_domain)
    system.l3 = L3Cache(clk_domain=system.cpu_clk_domain)
    system.tol2bus = L2XBar(clk_domain = system.cpu_clk_domain, width=64)
    system.tol3bus = L2XBar(clk_domain = system.cpu_clk_domain, width=64)

    system.l2.cpu_side = system.tol2bus.master
    system.l2.mem_side = system.tol3bus.slave

    system.l3.cpu_side = system.tol3bus.master
    system.l3.mem_side = system.membus.slave

    for cpu in system.cpu:
        cpu.addPrivateSplitL1CachesWithPlusI(L1_ICache(), L1_DCache(), L1_ICachePlus())
        cpu.createInterruptController()
        cpu.connectAllPorts(system.tol2bus, system.membus)

