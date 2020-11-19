#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import local_config as lc
import argparse

num_thread = lc.cores_per_task
benchmark_list_file = f'../benchmark-list/spec2017_on_{lc.machine_tag}.txt'
config = f'spec2017_simpoint_profile_full'
outdir = pjoin(lc.simpoint_base_dir, f'{config}')

def main():
    g5_configs = []

    dict_options = {
            # '--branch-trace-file': 'useless_branch.protobuf.gz',
            '--simpoint-interval': 200*10**6,
            '--arch': 'RISCV',
            '--spec-size': 'ref',
            }

    binary_options= [
            '--simpoint-profile',
            ]

    for benchmark in c.get_benchmarks(benchmark_list_file):
        cpt_id = 0
        task = f'{benchmark}_{cpt_id}'
        g5_config = c.G5Config(
                benchmark=benchmark,
                bmk_outdir=pjoin(outdir, task),
                cpt_id=int(cpt_id),
                arch='RISCV',
                full=True,
                full_max_insts=7*10**12,
                debug=False,
                func_id=config,
                mem_demand='16GB',
                simpoint=True,
                cpu_model='Atomic',
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

