import re
import sys

assert len(sys.argv) == 3

golden = []
test = []
pattern = re.compile('\d+ VCommitting \d+ instruction with sn:(\d+) PC:\((.+=>.+)\)\.\(0=>1\),' +
        ' with wb value: (.+), with v_addr: (.+)')

def match_and_append(p, line):
    m = p.match(line)
    if m:
        # print(m.group(1), m.group(2))
        return m.group(2), m.group(3), m.group(4)
    else:
        return None

with open(sys.argv[1]) as f, open(sys.argv[2]) as golden_f:
    count = 0
    for lt, lg in zip(f, golden_f):
        count += 1
        if count % 10000 == 0:
            print(count)

        mt = match_and_append(pattern, lt)
        mg = match_and_append(pattern, lg)
        if mt is None and mg is None:
            print('.')
            continue
        elif mt is None or mg is None:
            print(f"Difference found in line {count}, one is None!")

        pc, val, addr = mt
        pcg, valg, addrg = mg

        if pc != pcg or val != valg:
            if val == '1' and valg == '3':
                print('Ignore csrrs error')
            else:
                print(f"Difference found in line {count}")
                print("test:", pc, val, "gold:", pcg, valg, "addr:", addr)
                print("test:", lt, "gold:", lg, "addr:", addrg)
                break


