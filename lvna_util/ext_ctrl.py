import re
import os
import time
from common import *
import subprocess
import signal
import argparse

my_env = os.environ.copy()
my_env["LD_PRELOAD"] = '/nfs-nvme/home/share/debug/zhouyaoyang/libz.so.1.2.11.zlib-ng' \
+ os.pathsep + my_env.get("LD_PRELOAD","")

parser = argparse.ArgumentParser()
# parser.add_argument('-W','--warmup',type=int,default=50_000_000)
parser.add_argument('--epoch',type=int,default=10)
parser.add_argument('--hot',type=float,default=0.9)
args = parser.parse_args()

def run_once(bm, outdir, inc, warmup):
    os.makedirs(outdir,exist_ok=True)
    run_once_script = '/nfs/home/zhangchuanqi/lvna/5g/hw-branch/lvna_util/mix_nohype.py'
    cmd_base = ['python3', run_once_script]
    addition_cmd = []
    addition_cmd.append(f'-b={bm}')
    addition_cmd.append(f'-W={warmup}')
    # addition_cmd.append(f'-W=1000000')
    addition_cmd.append(f'-D={outdir}')
    addition_cmd.append(f'--l2inc=10000')
    addition_cmd.append(f'--l3inc={inc}')

    stdout_file = os.path.join(outdir, "stdout.log")
    stderr_file = os.path.join(outdir, "stderr.log")
    with open(stdout_file, "w") as stdout, open(stderr_file, "w") as stderr:
        run_cmd = cmd_base + addition_cmd
        cmd_str = " ".join(run_cmd)
        print(f"cmd: {cmd_str}")
        proc = subprocess.Popen(
            run_cmd, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr, \
                preexec_fn=os.setsid, env=my_env, text=True)
    return proc

def get_data(outdir):
    with open(os.path.join(outdir,'stderr.log')) as f:
        c = f.read()
    # data are printed in controlplane.cc:tuning()
    # so they are the data of the second 100W cycle
    # speedup = float(re.findall('speedup total:([0-9\.]+)', c)[0])
    jobipc = float(re.findall('jobIpcTotal:([0-9\.]+)', c)[0])
    bgipc = float(re.findall('bgIpcTotal:([0-9\.]+)', c)[0])
    print(f"info: get_data jobIpcTotal:{jobipc} bgIpcTotal:{bgipc}")
    oldinc = int(re.findall('old inc ([0-9\.]+)', c)[0])
    return jobipc, oldinc

def gen_set_est(outdir):
    new_env = os.environ.copy()
    new_env["PYTHONPATH"] = '/nfs/home/zhangchuanqi/lvna/5g/gem5_data_proc/'
    run_cmd = [
        'python3',
        '/nfs/home/zhangchuanqi/lvna/5g/gem5_data_proc/utils/job_set_extract.py',
        f'{outdir}',
        '2'
    ]
    proc = subprocess.run(run_cmd, env=new_env)

if __name__ == '__main__':
    os.chdir(ff_base)
    # bm = 'xal10-xal19-gcc10-gcc18'
    bm = 'gcc13-xal8-xal10-xal19'
    name = "bic_50M"
    warmup = 50000000
    epochs = args.epoch
    hot = args.hot
    outdirs = []
    for i in range(epochs):
        outdirs.append(os.path.join(ff_base, f'log/hwtest/{name}/{bm}/{hot}/{i}'))

    try:
        # start batch warmup
        procs = []
        for i in range(epochs):
            print(f"Start Warmup {i}")
            outdir = outdirs[i]
            procs.append(run_once(bm, outdir, 10000, warmup))

        # start QoS
        upper = 100
        lower = 0
        newinc = int((upper+lower)/2)
        initipc = 0

        for iter in range(epochs):
            print(f"Start Iter {iter}")
            outdir = outdirs[iter]

            # get no-ctrl situation as base
            if iter == 0:
                inputInc = '10000'
                # communicate will block until proc finishes
                procs[iter].communicate(inputInc)
                initipc, _ = get_data(outdir)
            else:
                inputInc = ' '.join([str(newinc),last_out_est,str(hot)])
                procs[iter].communicate(inputInc)
                ipc, oldinc = get_data(outdir)
                speedup = ipc/initipc

                # binary search for proper inc
                if speedup > 1.102: # more inc, in upper half
                    lower = oldinc

                elif speedup < 1.098: # less inc, in lower half
                    upper = oldinc

                newinc = int((upper+lower)/2)
                print(newinc)

            gen_set_est(outdir)
            last_out_est = os.path.join(outdir,"set_est")
    except KeyboardInterrupt:
        print("Interrupted. Exiting all programs ...")
        for proc in procs[iter:]:
            os.killpg(os.getpgid(proc.pid), signal.SIGINT)