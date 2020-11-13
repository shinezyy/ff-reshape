import argparse
import os
import os.path as osp
import sh

parser = argparse.ArgumentParser()
parser.add_argument('-c', '--cpu-dir', type=str, action='store', required=True)
parser.add_argument('-r', '--run-dir', type=str, action='store', required=True)
parser.add_argument('-s', '--size', type=str, action='store', required=True)
args = parser.parse_args()

run_dir_root = args.run_dir + '_' + args.size
if not osp.exists(run_dir_root):
    os.makedirs(run_dir_root)

for d in os.listdir(args.cpu_dir):
    code_name = d
    benchmark_name = d.split('.')[1]
    d = osp.join(args.cpu_dir, d)
    if d.endswith('specrand'):
        continue
    all_dir = osp.join(args.cpu_dir, code_name, 'data', 'all')
    size_dir = osp.join(args.cpu_dir, code_name, 'data', args.size)

    run_dir = osp.join(run_dir_root, benchmark_name)
    if not osp.exists(run_dir):
        os.makedirs(run_dir)

    print(f'-ar {all_dir}/ {run_dir}/'.split())
    print(f'-ar {size_dir}/ {run_dir}/'.split())

    if osp.isdir(all_dir):
        sh.rsync(f'-ar {all_dir}/ {run_dir}/'.split())
    if osp.isdir(size_dir):
        sh.rsync(f'-ar {size_dir}/ {run_dir}/'.split())

