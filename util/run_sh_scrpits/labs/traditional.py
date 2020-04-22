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

num_thread = 60

full = True

if full:
    d = '-full'
    insts = 220*10**6
else:
    d = ''
    insts = 19*10**6

window_size = 192

exp_options = [
        '--num-ROB', window_size,
        '--num-PhysReg', window_size,
        '--num-IQ', round(0.43 * window_size),
        '--num-LQ', round(0.32 * window_size),
        '--num-SQ', round(0.25 * window_size),
        # '--min-wk', 0,
        ]

outdir = f'{c.stats_base_dir}/trad_4w{d}/'

arch = 'RISCV'

def trad_4w(benchmark, some_extra_args, outdir_b, cpt_id):

    interval = 200*10**6
    warmup = 20*10**6

    os.chdir(c.gem5_exec('2017'))

    panic_tick = 84334713043000
    options = [
            '--stats-file=stats.txt',
            '--outdir=' + outdir_b,
            #'--debug-flags=Fetch,Decode,IEW,Commit,O3CPU,LSQ,LSQUnit',
            #'--debug-flags=Commit',
            #'--debug-flags=ValueCommit',
            #'--debug-start={}'.format  (panic_tick - 2000000),
            #'--debug-end={}'.format    (panic_tick + 2000000),
            pjoin(c.gem5_home(), 'configs/spec2017/se_spec17.py'),
            '--spec-2017-bench',
            '-b', '{}'.format(benchmark),
            '--benchmark-stdout={}/out'.format(outdir_b),
            '--benchmark-stderr={}/err'.format(outdir_b),
            '-I {}'.format(insts),
            # '-m', '254890101819500',
            # '--rel-max-tick=100',
            '--mem-size=16GB',
            '-r {}'.format(cpt_id + 1),
            '--restore-simpoint-checkpoint',
            '--checkpoint-dir={}'.format(pjoin(c.gem5_cpt_dir(arch, 2017),
                benchmark)),
            '--arch={}'.format(arch),
            '--spec-size=ref',
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
            '--use-zperceptron',
            *exp_options,
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


def run(benchmark_cpt_id):
    benchmark, cpt_id = benchmark_cpt_id
    dir_name = "{}_{}".format(benchmark, cpt_id)
    outdir_b = pjoin(outdir, dir_name)

    if not os.path.isdir(outdir_b):
        os.makedirs(outdir_b)

    cpt_flag_file = pjoin(c.gem5_cpt_dir(arch, 2017), benchmark,
            'ts-take_cpt_for_benchmark')
    prerequisite = os.path.isfile(cpt_flag_file)
    some_extra_args = None

    if prerequisite:
        print('cpt flag found, is going to run gem5 on', dir_name)
        c.avoid_repeated(trad_4w, outdir_b,
                pjoin(c.gem5_build(arch), 'gem5.opt'),
                benchmark, some_extra_args, outdir_b, cpt_id)
    else:
        print('prerequisite not satisified, abort on', dir_name)


def main():
    benchmarks = []

    with open('../all_function_spec2017.txt') as f:
        for line in f:
            if not line.startswith('#'):
                for i in range(0, 3):
                    benchmarks.append((line.strip(), i))

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(run, benchmarks)
    else:
        run(benchmarks[0])


if __name__ == '__main__':
    main()

