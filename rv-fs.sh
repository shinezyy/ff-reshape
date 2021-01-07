./build/RISCV/gem5.opt \
    --debug-flags=AddrRanges,Fault \
    ./configs/example/fs.py --mem-size=256MB \
    --generic-rv-cpt /home/zyy/projects/NEMU/outputs/take_cpt/microbench/12/_0.111888_.gz

    # --generic-rv-cpt /home/zyy/projects/NEMU/outputs/take_cpt/microbench/12/test.gz
    # --debug-flags=AddrRanges,SimpleCPU,ExecAll,Fault \
