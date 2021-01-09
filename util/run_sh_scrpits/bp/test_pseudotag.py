#!/usr/bin/env python3

import os
import subprocess
from test import run
import math as m

rv_origin = './rv-origin.py'
options = [rv_origin]

size   = 128
hislen = 127

p_bits = [4, 6, 8, 10]
print(p_bits)

num_threads = 6

for i in range(len(p_bits)):
    options += ['--num-threads={}'.format(num_threads),
                '--bp-size={}'.format(size),
                '--bp-history-len={}'.format(hislen-p_bits[i]),
                '--bp-pseudo-tagging={}'.format(p_bits[i]),
                '-a']
    run(options)
    options = [rv_origin]
