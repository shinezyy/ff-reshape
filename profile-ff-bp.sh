#!/bin/bash

TARGETS=("/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/astar_rivers_60000000_0.001307/0/_60000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/gcc_200_146840000000_0.021399/0/_146840000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/bzip2_program_370340000000_0.020905/0/_370340000000_.gz"
        "/nfs/home/share/checkpoints_profiles/spec06_rv64gc_o2_20m/take_cpt/libquantum_1003320000000_0.007505/0/_1003320000000_.gz")
MAXINSTS=5000000

ts=`date +%y-%d-%m-%T`
if [ ! -d "./rpt" ]; then
    mkdir ./rpt
fi
mkdir ./rpt/${ts}

count=0

# $1: count
# $2: name of checkpoint
# $3: BP type
write_rpt () {
    fn=./rpt/${ts}/$1.$3.rpt
    echo "Checkpoint: $2" > ${fn}
    date >> ${fn}
	grep ffBranchPred m5out/stats.txt >> ${fn}
}

for ckp in ${TARGETS[*]}; do
    echo "#${count} Running  ${ckp}"

    ./build/RISCV/gem5.opt --debug-flags=ForwardN ./configs/example/fs.py \
		--mem-size=8GB --maxinsts ${MAXINSTS} \
		--generic-rv-cpt ${ckp} \
		--caches --cpu-type=DerivO3CPU \
		--ff-bp-type=forwardN \
        --trace-forward-n 1000000:100 \
        --forward-n-numGTabBanks 6 \
        --forward-n-numGTabEntries 1024 \
        --forward-n-numBTabEntries 8192 \
        --forward-n-histLenInitial 10 \
        --forward-n-histLenGrowth 2 \
        --forward-n-randNumSeed 0
    write_rpt ${count} ${ckp} forward

    ./build/RISCV/gem5.opt --debug-flags=ForwardN ./configs/example/fs.py \
		--mem-size=8GB --maxinsts ${MAXINSTS} \
		--generic-rv-cpt ${ckp} \
		--caches --cpu-type=DerivO3CPU \
		--ff-bp-type=trivial
    write_rpt ${count} ${ckp} trivial

    let count++
done
