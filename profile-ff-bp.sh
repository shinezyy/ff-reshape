#!/bin/bash

TARGETS=("/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/astar_rivers_60000000_0.001307/0/_60000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/gcc_200_146840000000_0.021399/0/_146840000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/bzip2_program_370340000000_0.020905/0/_370340000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/libquantum_1003320000000_0.007505/0/_1003320000000_.gz")

if [ ! -d "./rpt" ]; then
   mkdir ./rpt
fi

count=0

for ckp in ${TARGETS[*]}; do
    echo "#${count} Running  ${ckp}"

    ./build/RISCV/gem5.opt --debug-flags=ForwardN ./configs/example/fs.py \
		--mem-size=8GB --maxinsts 5000000 \
		--generic-rv-cpt ${ckp} \
		--caches --cpu-type=DerivO3CPU \
		--ff-bp-type=forwardN \
        --trace-forward-n 1000000:100 \
		--forward-n-histlen 8

    echo "Checkpoint: ${ckp}" > ./rpt/${count}.rpt
    date >> ./rpt/${count}.rpt
	grep ffBranchPred m5out/stats.txt >> ./rpt/${count}.rpt
    let count++
done
