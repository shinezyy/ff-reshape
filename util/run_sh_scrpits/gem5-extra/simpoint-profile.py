#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import spec_common as sc
import local_config as lc
import argparse

num_thread = lc.cores_per_task
config = f'spec2017_simpoint_profile_full'
outdir = pjoin(lc.simpoint_base_dir, f'{config}')

def main():
    g5_configs = []
    spec = sc.Spec17Commands()

    dict_options = {
            # '--branch-trace-file': 'useless_branch.protobuf.gz',
            '--simpoint-interval': 200*10**6,
            '--arch': 'RISCV',
            '--spec-size': 'ref',
            }

    binary_options= [
            '--simpoint-profile',
            ]

    for task, cmd in spec.get_local().items():
        benchmark = task.split('_')[0]
        g5_config = c.G5Config(
                benchmark=benchmark,
                task=task,
                bmk_outdir=pjoin(outdir, task),
                cpt_id=0,
                arch='RISCV',
                full=True,
                full_max_insts=7*10**12,
                debug=False,
                func_id=config,
                mem_demand='16GB',
                simpoint=True,
                cpu_model='Atomic',
                cmd=cmd,
                spec_cmd_mode=True,
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

