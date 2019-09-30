#!/usr/bin/env python3

import os
import re
import sys
import random
import sh
import time
from os.path import join as pjoin
from os.path import expanduser as uexp
from multiprocessing import Pool
import common as c

tag = str(sh.git("describe")).strip()
lmd = 0.55
# tag = 'naive_pc'
# outdir =  f'/work/gem5-results/debug-pred-{tag}/'
outdir =  f'/work/gem5-results/op-rand-debug/'
arch = 'RISCV'
cycles_after = 100000000000
log_file = f'300-t-before-last-commit.txt'

large_enough = 2**62

def debug_1(benchmark, some_extra_args, outdir_b):

    benchmark, panic_tick = benchmark
    panic_tick = int(panic_tick)
    interval = 200*10**6
    warmup = 20*10**6

    os.chdir(c.gem5_exec())

    options = [
            '--outdir=' + outdir_b,
            '--stats-file=debug_stats.txt',
            # '--debug-flags=Fetch,DAllocation,DIEWC,IEW,Rename,Commit,DQRead,' + \
            #         'DQWrite,DQWake,FFCommit,FFSquash,DQ,FUW,ValueCommit,'+ \
            #         'Cache,DRAMSim2,MemoryAccess,DRAM,'+ \
            #         'FFInit,FFExec,FUSched,FanoutPred1,Reshape,ReadyHint',
            '--debug-flags=Fetch,DAllocation,DIEWC,IEW,Rename,Commit,DQRead,' + \
                    'DQWrite,DQWake,FFCommit,FFSquash,DQ,FUW,'+ \
                    'FFInit,FFExec,FUSched,FanoutPred1',
            # '--debug-flags=ValueCommit',
            '--debug-flags=RSProbe1',
            #'--debug-start={}'.format (102498195336000),
            '--debug-start={}'.format (panic_tick - 3000000),
            '--debug-end={}'.format   (panic_tick + 3000000),
            # '--debug-start={}'.format (large_enough-100),
            # '--debug-end={}'.format   (large_enough),
            pjoin(c.gem5_home(), 'configs/spec2006/se_spec06.py'),
            '--spec-2006-bench',
            '-b', '{}'.format(benchmark),
            '--benchmark-stdout={}/out'.format(outdir_b),
            '--benchmark-stderr={}/err'.format(outdir_b),
            '-I {}'.format(220*10**6),
            #'-I {}'.format(190*10**5),
            # '--rel-max-tick=100',
            '--mem-size=4GB',
            '-r 1',
            '--restore-simpoint-checkpoint',
            '--checkpoint-dir={}'.format(pjoin(c.gem5_cpt_dir(arch),
                benchmark)),
            '--arch={}'.format(arch),
            ]
    cpu_model = 'OoO'
    if cpu_model == 'TimingSimple':
        options += [
                '--cpu-type=TimingSimpleCPU',
                '--mem-type=SimpleMemory',
                ]
    elif cpu_model == 'OoO':
        options += [
            '--cpu-type=DerivFFCPU',
            '--mem-type=DDR3_1600_8x8',

            '--caches',
            '--cacheline_size=64',

            '--l1i_size=32kB',
            '--l1d_size=32kB',
            '--l1i_assoc=8',
            '--l1d_assoc=8',

            '--l2cache',
            '--l2_size=4MB',
            '--l2_assoc=8',
            '--num-ROB=192',
            '--num-IQ=60',
            '--num-LQ=72',
            '--num-SQ=42',
            '--num-PhysReg=168',
            '--use-zperceptron',
            f'--fanout-lambda={lmd}',
            # '--enable-reshape',
            # '--rand-op-position',
            # '--profit-discount=1.0',
            # '--ready-hint',
            ]
    else:
        assert False
    print(options)
    gem5 = sh.Command(pjoin(c.gem5_build(arch), 'gem5.opt'))
    # sys.exit(0)
    gem5(
            _out=pjoin(outdir_b, log_file),
            _err=pjoin(outdir_b, 'debug_err.txt'),
            *options
            )


def run(benchmark):
    outdir_b = pjoin(outdir, benchmark[0])
    if not os.path.isdir(outdir_b):
        os.makedirs(outdir_b)

    cpt_flag_file = pjoin(c.gem5_cpt_dir(arch), benchmark[0],
            'ts-take_cpt_for_benchmark')
    prerequisite = os.path.isfile(cpt_flag_file)
    some_extra_args = None

    if prerequisite:
        print('cpt flag found, is going to run gem5 on', benchmark[0])
        c.avoid_repeated(debug_1, outdir_b,
                pjoin(c.gem5_build(arch), 'gem5.opt'),
                benchmark, some_extra_args, outdir_b)
    else:
        print('prerequisite not satisified, abort on', benchmark[0])


def main():
    num_thread = 5

    benchmarks = []

    with open('./tick_around_debug.txt') as f:
        for line in f:
            if not line.startswith('#'):
                benchmarks.append(line.strip().split())

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(run, benchmarks)
    else:
        run(benchmarks[0])


if __name__ == '__main__':
    main()

