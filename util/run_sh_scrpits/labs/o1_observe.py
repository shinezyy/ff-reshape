#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import local_config as lc
import argparse

args = c.get_debug_options()

assert args.benchmark is not None

num_thread = lc.cores_per_task

lbuf_on = True
obp = True
full = True
benchmark_list_file = '../all_function_spec2017.txt'
benchmark_list_file = './tmp.txt'
debug = \
        True
debug_flags = [
        'ValueCommit',
        'NoSQSMB',
        'NoSQPred',
        'DQWake',
        'DQ',
        'FFSquash',
        'FFDisp',
        'DQGOF',
        'DQPair',
        'FUW',
        'DIEWC',
        'MemDepUnit',
        'Commit',
        'FFCommit',
        'LSQUnit',
        'Rename',
        'DAllocation',
        # 'CrossBar',
        ]
if args.inst_trace:
    debug_flags = [ 'ValueCommit', ]
if not (args.inst_trace or args.debug):
    debug = False
panic_tick = \
        args.debug_tick

if full:
    d = '_f'
else:
    d = '_s'
if obp:
    obp_suffix = '_o'
else:
    obp_suffix = '_l'
if lbuf_on:
    lbuf_suffix = '_lbuf'
else:
    lbuf_suffix = '_nbuf'

config = f'ob_o1_r_h{lbuf_suffix}{obp_suffix}'
outdir = f'{c.dev_stats_base_dir}/{config}{d}/'

def main():
    g5_configs = []

    dict_options = {
            '--trace-interval': args.ti,
            '--cpu-type': 'DerivFFCPU',
            '--dq-groups': 1,

            '--branch-trace-file': 'useless_branch.protobuf.gz',

            '--narrow-xbar-wk': 0,
            '--xbar-wk': 0,
            '--min-wk': 1,
            '--mem-squash-factor': 3,
            }
    if obp:
        dict_options['--use-bp'] = 'OracleBP'
    else:
        dict_options['--use-bp'] = 'LTAGE'

    binary_options= [
            '--rand-op-position',
            '--ready-hint',

            '--check-outcome-addr',
            '--branch-trace-en',
            '--fanout-lambda=0.5',
            ]

    if lbuf_on:
        binary_options.append('--enable-loop-buffer',)

    task = args.benchmark
    benchmark, cpt_id = task.split('_')
    g5_config = c.G5Config(
            benchmark=benchmark,
            bmk_outdir=pjoin(outdir, task),
            cpt_id=int(cpt_id),
            arch='RISCV',
            full=full,
            full_max_insts=220 * 10**6,
            debug=debug,
            debug_flags=debug_flags,
            panic_tick=panic_tick,
            func_id=config,
            )

    g5_config.add_options(binary_options)
    g5_config.update_options(dict_options)
    g5_configs.append(g5_config)

    if num_thread > 1:
        p = Pool(num_thread)
        p.map(c.run_wrapper, g5_configs)
    else:
        g5_configs[0].check_and_run()


if __name__ == '__main__':
    main()

