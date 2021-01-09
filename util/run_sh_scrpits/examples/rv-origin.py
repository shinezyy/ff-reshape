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
import subprocess
import common as c


current_git_tag = subprocess.check_output(
        ['git', 'describe']).strip().decode('utf8')
# print(current_git_tag)
# assert False

num_ROB = 300
num_IQ  = 128
num_LQ  = 100
num_SQ  = 100
num_SQ  = 100
num_PhysReg  = 256

outdir_1 = '/ramdisk/zyy/lab-results/' + \
        'RV-Ideal-ROB{}-LQ{}-SQ{}-Reg{}/'.format(
        num_ROB,
        num_LQ,
        num_SQ,
        num_PhysReg,
        )
outdir_2 = 'CAM-LTAGE-{}-IQ{}'.format(
        current_git_tag,
        num_IQ
        )

outdir = outdir_1 + outdir_2


arch = 'RISCV'

def rv_origin(benchmark, some_extra_args, outdir_b):

    interval = 200*10**6
    warmup = 20*10**6

    os.chdir(c.gem5_exec())

    start_tick  = 102501012844500
    end_tick    = 102501012945500
    options = [
            # '--debug-flags=O3CPUAll',
            # '--debug-flags=ThreePI,ReHash,TwoPhaseIssue,SSDepGraph,'
            # 'PostponedWake,TwoPhaseMDU,WakeSquashed,Rename,IEW,IQ,Commit,'
            # 'ROB,Decode,Fetch,O3CPU,ReHashD1',
            # '--debug-start={}'.format   (start_tick),
            # '--debug-end={}'.format     (end_tick),
            '--outdir=' + outdir_b,
            pjoin(c.gem5_home(), 'configs/spec2006/se_spec06.py'),
            '--spec-2006-bench',
            '-b', '{}'.format(benchmark),
            '--benchmark-stdout={}/out'.format(outdir_b),
            '--benchmark-stderr={}/err'.format(outdir_b),
            '-I {}'.format(220*10**6),
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
            '--cpu-type=DerivO3CPU',
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
            '--num-ROB={}'.format(num_ROB),
            '--num-IQ={}'.format(num_IQ),
            '--num-LQ={}'.format(num_LQ),
            '--num-SQ={}'.format(num_SQ),
            '--num-PhysReg={}'.format(num_PhysReg),
            ]
    else:
        assert False
    print(options)
    gem5 = sh.Command(pjoin(c.gem5_build(arch), 'gem5.opt'))
    # sys.exit(0)
    gem5(
            _out=pjoin(outdir_b, 'gem5_out.txt'),
            _err=pjoin(outdir_b, 'gem5_err.txt'),
            *options
            )


def run(benchmark):
    outdir_b = pjoin(outdir, benchmark)
    if not os.path.isdir(outdir_b):
        os.makedirs(outdir_b)

    cpt_flag_file = pjoin(c.gem5_cpt_dir(arch), benchmark,
            'ts-take_cpt_for_benchmark')
    prerequisite = os.path.isfile(cpt_flag_file)
    some_extra_args = None

    if prerequisite:
        print('cpt flag found, is going to run gem5 on', benchmark)
        c.avoid_repeated(rv_origin, outdir_b,
                benchmark, some_extra_args, outdir_b)
    else:
        print('prerequisite not satisified, abort on', benchmark)


def main():
    num_thread = 22

    benchmarks = []

    with open('./all_function_spec.txt') as f:
        for line in f:
            benchmarks.append(line.strip())

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(run, benchmarks)
    else:
        run(benchmarks[0])


if __name__ == '__main__':
    main()
