# What's different?

`configs/example/fs.py` is modified to support Generic Checkpoint (GCpt) for RISC-V

We implemented the minimum required devices (timer, sdcard, and uartlite) to boot Linux.
Our memory space layout is different Sifive's:
```
mmio map 'clint' at     [0x38000000, 0x3800ffff]
mmio map 'sdhci' at     [0x40002000, 0x4000207f]
mmio map 'uartlite' at  [0x40600000, 0x4060000c]
Main memory is at       [0x80000000, 0x280000000]
```

Set `--generic-rv-cpt=/path/to/the memory image/gzip/file` to restore from the GCpt;
Set `--gcpt-warmup=N` to reset and dump stats after warmup with `N` instructions;

# The original READMD
This is the gem5 simulator.

The main website can be found at http://www.gem5.org

A good starting point is http://www.gem5.org/about, and for
more information about building the simulator and getting started
please see http://www.gem5.org/documentation and
http://www.gem5.org/documentation/learning_gem5/introduction.

To build gem5, you will need the following software: g++ or clang,
Python (gem5 links in the Python interpreter), SCons, SWIG, zlib, m4,
and lastly protobuf if you want trace capture and playback
support. Please see http://www.gem5.org/documentation/general_docs/building
for more details concerning the minimum versions of the aforementioned tools.

Once you have all dependencies resolved, type 'scons
build/<ARCH>/gem5.opt' where ARCH is one of ARM, NULL, MIPS, POWER, SPARC,
or X86. This will build an optimized version of the gem5 binary (gem5.opt)
for the the specified architecture. See
http://www.gem5.org/documentation/general_docs/building for more details and
options.

The basic source release includes these subdirectories:
   - configs: example simulation configuration scripts
   - ext: less-common external packages needed to build gem5
   - src: source code of the gem5 simulator
   - system: source for some optional system software for simulated systems
   - tests: regression tests
   - util: useful utility programs and files

To run full-system simulations, you will need compiled system firmware
(console and PALcode for Alpha), kernel binaries and one or more disk
images.

If you have questions, please send mail to gem5-users@gem5.org

Enjoy using gem5 and please share your modifications and extensions.
