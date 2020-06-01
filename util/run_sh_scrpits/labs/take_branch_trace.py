#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import local_config as lc

num_thread = lc.cores_per_task
full = False
if full:
    d = '-full'
else:
    d = ''
outdir = f'{c.stats_base_dir}/take-branch-trace{d}/'

dict_options = {
            '--branch-trace-file': 'branch.protobuf.gz',
            }

binary_options= [
        '--branch-trace-en',
        ]



def main():
    g5_configs = []

    #with open('../all_function_spec2017.txt') as f:
    with open('./tmp.txt') as f:
        for line in f:
            if not line.startswith('#'):
                for cpt_id in range(0, 3):
                    benchmark = line.strip()
                    g5_config = c.G5Config(
                        benchmark=benchmark,
                        bmk_outdir=pjoin(outdir, benchmark + '_' + str(cpt_id)),
                        cpt_id=cpt_id,
                        arch='RISCV',
                        full=full,
                        debug=True,
                        debug_flags=[
                            'BranchResolve',
                            ],
                        func_id='take_branch_trace',
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

