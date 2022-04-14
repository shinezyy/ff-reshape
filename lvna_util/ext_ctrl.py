import re
import os
import time
from common import *
import subprocess


if __name__ == '__main__':
    os.chdir(ff_base)
    bm = 'xal10-xal19-gcc10-gcc18'
    name = "bic"

    upper = 100
    lower = 0
    newinc = int((upper+lower)/2)

    for iter in range(10):
        outdir_name = 'log/hwtest/{}_{}_{}'.format(bm, name, iter)
        outdir = os.path.join(ff_base, outdir_name)
        os.makedirs(outdir,exist_ok=True)
        run_once_script = '/nfs/home/zhangchuanqi/lvna/5g/ff-reshape/lvna_util/mix_nohype.py'
        cmd_base = ['python3', run_once_script]
        addition_cmd = []
        addition_cmd.append(f'-b={bm}')
        addition_cmd.append(f'-W=20000000')
        # addition_cmd.append(f'-W=1000000')
        addition_cmd.append(f'-D={outdir}')
        addition_cmd.append(f'--l2inc=10000')
        addition_cmd.append(f'--l3inc={newinc}')

        stdout_file = os.path.join(outdir, "stdout.log")
        stderr_file = os.path.join(outdir, "stderr.log")
        with open(stdout_file, "w") as stdout, open(stderr_file, "w") as stderr:
            run_cmd = cmd_base + addition_cmd
            cmd_str = " ".join(run_cmd)
            print(f"cmd: {cmd_str}")
            proc = subprocess.run(
                run_cmd, stdout=stdout, stderr=stderr, preexec_fn=os.setsid
                ,env=my_env)

        with open(os.path.join(outdir,'stderr.log')) as f:
            c = f.read()
        speedup = float(re.findall('speedup total:([0-9\.]+)', c)[0])
        oldinc = int(re.findall('old inc ([0-9\.]+)', c)[0])

        # binary search for proper inc
        if speedup > 1.102: # more inc, in upper half
            lower = oldinc

        elif speedup < 1.098: # less inc, in lower half
            upper = oldinc

        newinc = int((upper+lower)/2)
        print(newinc)