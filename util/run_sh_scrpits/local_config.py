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

# stats_base_dir = osp.expanduser('~/gem5-results-reuse')
stats_base_dir = osp.expanduser('~/gem5-results-hpca-rebuttal')

branch_outcome_dir = osp.expanduser('~/research-data/spec2017_branch_outcome')

total_cores = 128
n_tasks = 1
spare_cores = 1

cores_per_task = (total_cores - spare_cores) // n_tasks
