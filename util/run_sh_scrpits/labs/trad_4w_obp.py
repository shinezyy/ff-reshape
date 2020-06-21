#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import local_config as lc

num_thread = lc.cores_per_task

obp = True
full = True
if full:
    d = '_full'
else:
    d = ''
if obp:
        obp_suffix = '_obp'
else:
        obp_suffix = ''

config = f'trad_4w_small_iq{obp_suffix}'
outdir = f'{c.stats_base_dir}/{config}{d}/'

def main():
    g5_configs = []

    dict_options = {
            '--use-bp': 'OracleBP',
            '--branch-trace-file': 'useless_branch.protobuf.gz',
                        '--num-IQ': 48,
            }
    binary_options= [
            '--check-outcome-addr',
            '--branch-trace-en',
            ]


    #with open('./tmp.txt') as f:
    with open('../all_function_spec2017.txt') as f:
        for line in f:
            if not line.startswith('#'):
                for cpt_id in range(0, 3):
                    benchmark = line.strip()
                    task = benchmark + '_' + str(cpt_id)
                    g5_config = c.G5Config(
                        benchmark=benchmark,
                        bmk_outdir=pjoin(outdir, task),
                        cpt_id=cpt_id,
                        arch='RISCV',
                        full=full,
                        full_max_insts=220 * 10**6,
                        debug=False,
                        debug_flags=[
                            'Fetch',
                            ],
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

