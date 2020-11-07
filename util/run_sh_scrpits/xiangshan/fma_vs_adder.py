#!/usr/bin/env python3

import sys
sys.path.append('../')

from os.path import join as pjoin
from multiprocessing import Pool
import common as c
import local_config as lc

num_thread = lc.cores_per_task
window_size = 128 * 1

lbuf_on = True
obp = False
full = True
debug = False
simpoint_list = [0, 1, 2]
# simpoint_list = [1]
benchmark_list_file = '../all_function_spec2017.txt'
#benchmark_list_file = './tmp.txt'

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

l1ip = '_l1iplus'
#l1ip = '_nol1iplus'

# config = f'ideal_8w{obp_suffix}'
config = f'fadd2_fma3{lbuf_suffix}{obp_suffix}{l1ip}'
outdir = f'{lc.xiangshan_stats_base_dir}/{config}{d}/'

def main():
    g5_configs = []

    dict_options = {
            '--o3-core-width': 4,

            '--branch-trace-file': 'useless_branch.protobuf.gz',
            '--cpu-type': 'XiangshanCore',
            }

    if obp:
        dict_options['--use-bp'] = 'OracleBP'
    else:
        dict_options['--use-bp'] = 'LTAGE'

    binary_options= [
            '--check-outcome-addr',
            '--branch-trace-en',
            ]
    if lbuf_on:
        binary_options.append(
                '--enable-loop-buffer',
                )

    if l1ip == '_l1iplus':
        binary_options.append(
                '--l1i_plus',
                )

    with open(benchmark_list_file) as f:
        for line in f:
            if not line.startswith('#'):
                for cpt_id in simpoint_list:
                    benchmark = line.strip()
                    task = benchmark + '_' + str(cpt_id)
                    g5_config = c.G5Config(
                        benchmark=benchmark,
                        window_size=window_size,
                        bmk_outdir=pjoin(outdir, task),
                        cpt_id=cpt_id,
                        arch='RISCV',
                        full=full,
                        full_max_insts=220 * 10**6,
                        debug=debug,
                        debug_flags=[
                            'LoopBufferStack',
                            'Fetch',
                            'LoopBuffer',
                            ],
                        func_id=config,
                        config_script='configs/spec2017/xiangshan_spec2017.py'
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

