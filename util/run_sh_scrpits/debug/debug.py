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

lmd = 0.55

arch = 'RISCV'

num_thread = 5

full = False

if full:
    d = '-full'
    insts = 220*10**6
else:
    d = ''
    insts = 19*10**6

outdir = pjoin(c.stats_base_dir, f'omega-md-debug{d}')

exp_options = [
        #'--enable-reshape',
        '--rand-op-position',
        #'--profit-discount=1.0',
        #'--ready-hint',
        ]


def debug(benchmark, some_extra_args, outdir_b):

    interval = 200*10**6
    warmup = 20*10**6

    os.chdir(c.gem5_exec('2017'))

    panic_tick = 254980806320500
    options = [
            '--outdir=' + outdir_b,
            '--stats-file=debug_stats.txt',
            #'--debug-flags=DQV2',
            #'--debug-flags=ValueCommit',
            #'--debug-flags=FFExec,FFCommit,FFDisp,DAllocation,DQGOF',
            #'--debug-flags=DQWake,DQGDL,Rename,DQPair,FFSquash,FFExec,IEW',
            #'--debug-flags=LSQUnit,Cache', # memory
            '--debug-flags=FUW,FUPipe', # FU

            #'--debug-start={}'.format (panic_tick - 2000000),
            #'--debug-end={}'.format   (panic_tick + 200000),
            pjoin(c.gem5_home(), 'configs/spec2017/se_spec17.py'),
            '--spec-2017-bench',
            '-b', '{}'.format(benchmark),
            '--benchmark-stdout={}/out'.format(outdir_b),
            '--benchmark-stderr={}/err'.format(outdir_b),
            '-I {}'.format(insts),
            # '-m', '254890101819500',
            # '--rel-max-tick=100',
            '--mem-size=4GB',
            '-r 1',
            '--restore-simpoint-checkpoint',
            '--checkpoint-dir={}'.format(pjoin(c.gem5_cpt_dir(arch,2017),
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
            *exp_options,
            ]
    else:
        assert False
    print(options)
    gem5 = sh.Command(pjoin(c.gem5_build(arch), 'gem5.opt'))
    # sys.exit(0)
    gem5(
            _out=pjoin(outdir_b, 'debug_out.txt'),
            _err=pjoin(outdir_b, 'debug_err.txt'),
            *options
            )


def run(benchmark):
    outdir_b = pjoin(outdir, benchmark)
    if not os.path.isdir(outdir_b):
        os.makedirs(outdir_b)

    cpt_flag_file = pjoin(c.gem5_cpt_dir(arch, 2017), benchmark,
            'ts-take_cpt_for_benchmark')
    prerequisite = os.path.isfile(cpt_flag_file)
    some_extra_args = None

    if prerequisite:
        print('cpt flag found, is going to run gem5 on', benchmark)
        c.avoid_repeated(debug, outdir_b,
                pjoin(c.gem5_build(arch), 'gem5.opt'),
                benchmark, some_extra_args, outdir_b)
    else:
        print('prerequisite not satisified, abort on', benchmark)


def main():
    num_thread = 2

    benchmarks = []

    with open('./tmp.txt') as f:
        for line in f:
            if not line.startswith('#'):
                b, n = line.strip().split()
                benchmarks.append((b, int(n)))

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(run, benchmarks)
    else:
        run(benchmarks[0])


if __name__ == '__main__':
    main()

