#!/usr/bin/env python3
import os
import argparse
import time
from common import *

parser = argparse.ArgumentParser()
parser.add_argument('-b','--benchmark', type=str, required=True,help="like gcc-xal-xal-xal")
parser.add_argument('--l3_waymask_set', type=str, help="like 0xff,0xff00")
parser.add_argument('-I','--insts',type=int,default=50_000_000)
parser.add_argument('--cycle_afterwarm',type=int,default=10_000_000)
parser.add_argument('--set_est_dir',type=str)
parser.add_argument('-n','--np',type=int,default=4)
parser.add_argument('-W','--warmup',type=int,default=50_000_000)
parser.add_argument('-D','--output',type=str,default=None,help='output dir')
parser.add_argument('--debug-flag',type=str)
parser.add_argument('-C','--compile', action="store_true",help="compile Gem5 first")
parser.add_argument('--l2inc',type=int,default=10000)
parser.add_argument('--l3inc',type=int,default=10000) # this will be overided in Simulaion.py:repeatJobs()
parser.add_argument('--l2_tb_size',type=int,default=1024)
parser.add_argument('--l3_tb_size',type=int,default=2048)
args = parser.parse_args()

os.chdir(ff_base)
binary = os.path.join(ff_base,'build/RISCV/gem5.opt')
if args.compile:
    os.system('python3 `which scons` '+ binary+ ' -j64')

# ==================  Basics  ==================
if args.output is None:
    outdir = os.path.join(ff_base,'log/{}'.format(args.benchmark))
else:
    outdir = args.output
outopt = '--outdir='+outdir
debugf = '--debug-flag='+ args.debug_flag if args.debug_flag else ''
fspy   = os.path.join(ff_base,'configs/example/fs.py')

# ==================  Options  ==================
opt = []
opt.append('--nohype --branch-trace-file=useless_branch.protobuf.gz')
opt.append('--num-cpus={}'.format(args.np))
opt.append('--cpu-type=DerivO3CPU --num-ROB=192 --num-PhysReg=192 --num-IQ=192 --num-LQ=72 --num-SQ=48')
# opt.append('--mem-type=DRAMsim3')
opt.append('--mem-type=DDR4_2400_16x4')
opt.append('--mem-size={}GB'.format(args.np * 8))
# opt.append('--mem-channels=2')

opt.append('--cacheline_size=64')
opt.append('--sharel2')
opt.append('--caches --l2cache --l3_cache')
opt.append('--l1i_size=64kB --l1i_assoc=4')
opt.append('--l1d_size=32kB --l1d_assoc=8')

opt.append('--l2_size=768kB --l2_assoc=12')
opt.append('--sharel2')
opt.append('--l3_size=2MB --l3_assoc=8')
opt.append('--l2_slices=1024')
opt.append('--l3_slices=4096')
opt.append('--l2inc={} --l3inc={}'.format(args.l2inc, args.l3inc))
opt.append('--l2_tb_size={} --l3_tb_size={}'.format(args.l2_tb_size, args.l3_tb_size))

if args.l3_waymask_set:
    opt.append('--l3_waymask_set="{}"'.format(args.l3_waymask_set))

if args.set_est_dir:
    opt.append('--set_est_dir={}'.format(args.set_est_dir))

gcpt_all = [(os.path.join(benchmark_dir, benchmark_cpt_file[bm])) for bm in args.benchmark.split("-")]
opt.append('--job-benchmark')

# use "" around multiple paths connnected by ;
opt.append('--generic-rv-cpt=' + '"' + ";".join(gcpt_all) + '"')
opt.append('--gcpt-restorer=' + gcpt_bin_path)

opt.append('--gcpt-warmup={}'.format(args.warmup))
opt.append('--cycle_afterwarm={}'.format(args.cycle_afterwarm))


# ==================  RUN  ==================
cmd = [binary, outopt, debugf, fspy]
cmd.extend(opt)
print(" ".join(cmd))
os.system("echo starttime:"+time.strftime("%Y-%m-%d_%H:%M:%S", time.localtime())+">"+outdir+"/timestamp")
os.system(" ".join(cmd))
os.system("echo endtime:"+time.strftime("%Y-%m-%d_%H:%M:%S", time.localtime())+">>"+outdir+"/timestamp")
