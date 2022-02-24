benchmark_cpt_file = {
             'gcc':'gcc_ref32_O5_22850000000_0.197858/0/_6850000000_.gz',
             'gcc2':'gcc_ref32_O5_99150000000_0.170762/0/_3150000000_.gz',
             'xal':'xalancbmk_144250000000_0.153516/0/_250000000_.gz',
             'xal2':'xalancbmk_918450000000_0.10609/0/_6450000000_.gz',
             'xal3':'xalancbmk_550750000000_0.12401/0/_6750000000_.gz',
             'xal4':'xalancbmk_1041650000000_0.0838451/0/_1650000000_.gz'
}

# like 'gcc':'gcc_ref32_O5_22850000000_0.197858/
benchmarks = {k:v.split('/')[0] for k,v in benchmark_cpt_file.items() }

# ff_base = '/nfs/home/chenxi/ff-reshape/'
# benchmark_dir = '/nfs/home/share/checkpoints_profiles/nemu_take_simpoint_cpt_17/'
# gcpt_bin_path = '/nfs/home/share/checkpoints_profiles/gcpt.bin'
ff_base = '/home/chenxi/ff-reshape/'
benchmark_dir = '/home/zcq/lvna/5g/research-data/checkpoints_profiles/nemu_take_simpoint_cpt_17/'
gcpt_bin_path = '/home/zcq/lvna/5g/research-data/gcpt.bin'