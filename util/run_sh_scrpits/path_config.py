import os.path as osp


cpt_dirs = {
    2006: {
        'ARM': None,
        'RISCV': None,
    },
    2017: {
        'ARM': None,
        'RISCV': osp.expanduser('~/research-data/shrink_spec2017_simpoint_cpts'),
    },
}

stats_base_dir = osp.expanduser('~/gem5-results-2017')
