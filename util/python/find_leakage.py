import re

dispatched = set()
destroyed = set()
dispatch_pattern = re.compile('\d+: system.switch_cpus.diewc: Dispatching inst\[(\d+)\]')
destroy_pattern = re.compile('\d+: global: DynInst: \[sn:(\d+)\] Instruction destroyed')

def match_and_append(pattern, l):
    m = pattern.match(line)
    if m:
        # print(m.group(1))
        l.add(m.group(1))

with open('./gem5_out.txt') as f:
    for line in f:
        match_and_append(dispatch_pattern, dispatched)
        match_and_append(destroy_pattern, destroyed)
leakage = [int(x) for x in dispatched.difference(destroyed)]
print(sorted(leakage))

