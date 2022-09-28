#!/usr/bin/env python3

import os
import argparse
import time
import json

ff_base = '/nfs/home/zhangchuanqi/lvna/5g/ff-reshape/'

parser = argparse.ArgumentParser()
parser.add_argument('-b','--benchmark', type=str, required=True,help="like gcc-xal-xal-xal")
parser.add_argument('--l3_waymask_set', type=str, help="like ff-ff00")
parser.add_argument('--l3_waymask_high_set', type=str, help="like ff-ff00")
parser.add_argument('--l2_waymask_set', type=str, help="like ff-ff00")
parser.add_argument('-I','--insts',type=int,default=10_000_000)
parser.add_argument('--cycle_afterwarm',type=int,default=None)
parser.add_argument('--cycle_per_tti',type=int,default=None)
parser.add_argument('--set_est_dir',type=str)
parser.add_argument('-n','--np',type=int,default=1)
parser.add_argument('-W','--warmup',type=int,default=50_000_000)
parser.add_argument('-D','--output',type=str,default='',help='output dir')
parser.add_argument('--debug-flag',type=str)
parser.add_argument('-C','--compile', action="store_true",help="compile Gem5 first")
parser.add_argument('--l2_tb_freq',type=int,default=256)
parser.add_argument('--l2_tb_inc',type=int,default=1024)
parser.add_argument('--l2_tb_size',type=int,default=1024)
parser.add_argument('--l3_tb_freq',type=int,default=256)
parser.add_argument('--l3_tb_inc',type=int,default=1024)
parser.add_argument('--l3_tb_size',type=int,default=1024)
parser.add_argument('--l3_size_MB',type=int,default=2)
parser.add_argument('--num_tti',type=int,default=None)
parser.add_argument('--single_mode',action='store_true',default=False)
parser.add_argument('--enable-clint-sets',type=str,default=None)
parser.add_argument('--cpt-json',type=str,
default="/nfs/home/zhangchuanqi/lvna/5g/DirtyStuff/resources/simpoint_cpt_desc/06_max_redis_path.json")
args = parser.parse_args()

path_file = args.cpt_json

with open(path_file) as f:
    benchmark_cpt_file = json.load(f)


gcpt_bin_path = '/nfs/home/zhangchuanqi/lvna/for_xs/xs-env/NEMU/resource/gcpt_restore/build/gcpt-ori.bin'

if args.compile:
    os.system('python3 `which scons` '+ff_base+'build/RISCV/gem5.opt -j64')

# ==================  Basics  ==================
binary = ff_base+'build/RISCV/gem5.opt'
outdir = ff_base+'log/{}'.format(args.benchmark) if args.output=='' else args.output
outopt = '--outdir='+outdir
debugf = '--debug-flag='+ args.debug_flag if args.debug_flag else ''
fspy   = ff_base+'configs/example/fs.py'

# ==================  Options  ==================
opt = []

# opt.append('--nemu-diff')

opt.append('--branch-trace-file=useless_branch.protobuf.gz')
opt.append('--nohype')
opt.append('--num-cpus={}'.format(args.np))
opt.append('--cpu-type=DerivO3CPU --num-ROB=192 --num-PhysReg=192 --num-IQ=192 --num-LQ=72 --num-SQ=48')
# opt.append('--mem-type=DRAMsim3')
opt.append('--mem-type=DDR4_2400_16x4')
opt.append('--mem-size={}GB'.format(args.np * 8))
# opt.append('--mem-channels=2')

opt.append('--cacheline_size=64')
opt.append('--caches --l2cache --l3_cache')
opt.append('--l1i_size=64kB --l1i_assoc=4')
opt.append('--l1d_size=32kB --l1d_assoc=8')

opt.append('--l2_size=768kB --l2_assoc=12')
if args.np != 1:
    opt.append('--sharel2')
opt.append('--l2_slices=1024')
opt.append(f'--l3_size={args.l3_size_MB}MB --l3_assoc=8')
opt.append('--l3_slices={}'.format(2048*args.l3_size_MB))

# opt.append('--incll3')
# opt.append('--l1d-hwp-type=StridePrefetcher')
# opt.append('--l2-hwp-type=BOPPrefetcher')

opt.append('--l2_tb_size={} --l3_tb_size={}'.format(args.l2_tb_size, args.l3_tb_size))
opt.append('--l2_tb_freq={} --l3_tb_freq={}'.format(args.l2_tb_freq, args.l3_tb_freq))
opt.append('--l2_tb_inc={} --l3_tb_inc={}'.format(args.l2_tb_inc, args.l3_tb_inc))

if args.l3_waymask_set:
    opt.append('--l3_waymask_set="{}"'.format(args.l3_waymask_set))
if args.l3_waymask_high_set:
    opt.append('--l3_waymask_high_set="{}"'.format(args.l3_waymask_high_set))
if args.l2_waymask_set:
    opt.append('--l2_waymask_set="{}"'.format(args.l2_waymask_set))

if args.set_est_dir:
    opt.append('--set_est_dir={}'.format(args.set_est_dir))

gcpt_all = [benchmark_cpt_file[bm] for bm in args.benchmark.split("-")]
# opt.append('--job-benchmark')

# use "" around multiple paths connnected by ;
opt.append('--generic-rv-cpt=' + '"' + ";".join(gcpt_all) + '"')
opt.append('--gcpt-restorer=' + gcpt_bin_path)

opt.append('--gcpt-warmup={}'.format(args.warmup))
if args.cycle_afterwarm:
    opt.append('--cycle_afterwarm={}'.format(args.cycle_afterwarm))
elif args.num_tti:
    opt.append('--num_tti={}'.format(args.num_tti))
else:
    opt.append('--maxinsts0={}'.format(2*args.warmup, args.warmup))

if args.cycle_per_tti:
    opt.append('--cycle_per_tti={}'.format(args.cycle_per_tti))
if args.enable_clint_sets:
    opt.append('--enable-clint-sets="{}"'.format(args.enable_clint_sets))

opt.append('--mmc-img=/nfs/home/zhangchuanqi/lvna/new_micro/tail-sd.img')

# ==================  RUN  ==================
cmd = [binary, outopt, debugf, fspy]
cmd.extend(opt)
print(" ".join(cmd))
os.system(" ".join(cmd))
os.system("echo "+time.strftime("%Y-%m-%d_%H:%M:%S", time.localtime())+">"+outdir+"/timestamp")
