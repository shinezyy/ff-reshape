import re
import os
import time
from common import *

if __name__ == '__main__':
    os.chdir(ff_base)
    bm = 'xal10-xal19-gcc10-gcc18'
    name = "bic"

    upper = 100
    lower = 0
    newinc = int((upper+lower)/2)

    for iter in range(10):
        outdir = 'log/{}_{}_{}'.format(bm, name, iter)
        os.system('mkdir '+outdir)
        os.system('python3 lvna_util/mix_nohype.py -b={0} --l2inc=10000 --l3inc={2} \
            -W=1000000 -D={1} > {1}/run_log 2>&1'.format(bm, outdir, newinc))

        with open(outdir+'/run_log') as f:
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