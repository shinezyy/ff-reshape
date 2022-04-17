import re
import os
import time
from common import *
import subprocess

def run_once(bm, outdir, inc, warmup):
    outdir = os.path.join(ff_base, outdir)
    os.makedirs(outdir,exist_ok=True)
    run_once_script = '/nfs/home/chenxi/ff-reshape/lvna_util/mix_nohype.py'
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
                preexec_fn=os.setsid, env=None)
    return proc

def get_data(outdir):
    with open(os.path.join(outdir,'stderr.log')) as f:
        c = f.read()
    # data are printed in controlplane.cc:tuning()
    # so they are the data of the second 100W cycle
    speedup = float(re.findall('speedup total:([0-9\.]+)', c)[0])
    jobipc = float(re.findall('jobIpcTotal:([0-9\.]+)', c)[0])
    oldinc = int(re.findall('old inc ([0-9\.]+)', c)[0])
    return jobipc, oldinc

if __name__ == '__main__':
    os.chdir(ff_base)
    # bm = 'xal10-xal19-gcc10-gcc18'
    bm = 'gcc13-xal8-xal10-xal19'
    name = "bic_10M"
    warmup = 10000000
    epochs = 10

    # start batch warmup
    procs = []
    for i in range(epochs):
        print(f"Start Warmup {i}")
        outdir = f'log/hwtest/{bm}_{name}_{i}'
        procs.append(run_once(bm, outdir, 10000, warmup))

    # start QoS
    upper = 100
    lower = 0
    newinc = int((upper+lower)/2)
    initipc = 0

    for iter in range(epochs):
        print(f"Start Iter {iter}")
        outdir = f'log/hwtest/{bm}_{name}_{iter}'

        # get no-ctrl situation as base
        if iter == 0:
            inputInc = b'10000'
            # communicate will block until proc finishes
            procs[iter].communicate(inputInc)
            initipc, _ = get_data(outdir)
        else:
            inputInc = str(newinc).encode('UTF-8')
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