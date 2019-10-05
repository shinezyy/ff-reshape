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


# Please set to the directory where gem5-generated bbvs stored and
# ensure that you have performed simpoint clustering on them
simpoint_profile_dir = 'deadbeaf'
assert simpoint_profile_dir != 'deadbeaf'

# Please set to the directory where to store gem5-generated checkpoints
outdir = 'deadbeaf'
assert outdir != 'deadbeaf'


def take_cpt_for_benchmark(benchmark, simpoint_file, weight_file, outdir_b):

    gem5_dir = c.gem5_home()

    interval = 200*10**6
    warmup = 20*10**6

    exec_dir = c.gem5_exec()
    os.chdir(exec_dir)

    options = [
            '--outdir=' + outdir_b,
            pjoin(gem5_dir, 'configs/spec2006/se_spec06.py'),
            '--spec-2006-bench',
            '-b',
            '{}'.format(benchmark),
            '--benchmark-stdout={}/out'.format(outdir_b),
            '--benchmark-stderr={}/err'.format(outdir_b),
            '--cpu-type=AtomicSimpleCPU',
            '--fastmem',
            '--mem-size=4GB',
            '--take-simpoint-checkpoint={},{},{},{}'.format(
                simpoint_file, weight_file, interval, warmup)
            ]
    print(options)
    gem5 = sh.Command(pjoin(c.gem5_build(), 'gem5.opt'))
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

    simpoint_dir_b = pjoin(simpoint_profile_dir, benchmark)

    simpoint_file = pjoin(simpoint_dir_b, 'simpoints-final')
    weight_file = pjoin(simpoint_dir_b, 'weights-final')

    profiled = os.path.isfile(simpoint_file) and os.path.isfile(weight_file)

    if profiled:
        print('simpoint weight file found in {},'.format(simpoint_dir_b),
                'is going take simpoint cpt')
        c.avoid_repeated(take_cpt_for_benchmark, outdir_b,
                benchmark, simpoint_file, weight_file, outdir_b)
    else:
        print('simpoint weight file not found in {}, abort'.format(
            simpoint_dir_b))


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
