import sys
import re

assert len(sys.argv) == 2

ptr = '(\(\d \d+\) \(\d\))'
p_def = re.compile(f'\d+: arch_state: Inst\[(\d+)\] defines reg\[.+\] {ptr}')
p_use = re.compile(f'\d+: arch_state: Inst\[(\d+)\] forward reg\[.+\]from {ptr} to {ptr}')
p_exe = re.compile(f'(\d+): system\.switch_cpus\.diewc: Executing inst\[(\d+)\]')
p_drr = re.compile(f'\d+: arch_state: Inst\[(\d+)\] read reg\[.+\] .*from.*RF')

defs = {}

with open(sys.argv[1]) as f:
    for line in f:

        m = p_def.match(line)
        if m:
            inst = m.group(1)
            ptr = m.group(2)
            defs[ptr] = inst
            continue

        m = p_drr.match(line)
        if m:
            print(f'{m.group(1)} -> ready')
            continue

        m = p_use.match(line)
        if m:
            inst = m.group(1)
            src = m.group(2)
            dest = m.group(3)
            defs[dest] = defs.get(src, '0')
            print(f'{inst} -> {defs[dest]}')
            continue

        m = p_exe.match(line)
        if m:
            inst = m.group(2)
            print(f'{inst} Exec @ {m.group(1)} !')
            continue
