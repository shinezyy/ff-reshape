from m5.params import *
from m5.proxy import *

from m5.objects.Device import BasicPioDevice


class Lint(BasicPioDevice):
    type = 'Lint'
    cxx_header = "dev/riscv/lint.hh"
    cxx_class = 'gem5::Lint'
    time = Param.Time('01/01/2019', "System time to use ('Now' for real time)")
    pio_addr = 0x38000000
    pio_size = Param.Addr(0x10000, "Lint space size")
