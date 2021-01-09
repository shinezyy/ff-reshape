import re
import sys

assert len(sys.argv) == 3

pattern = re.compile('@(\d+) Committing (\d+) instruction with sn:(\d+) PC:\((.+=>.+)\)\.\(0=>1\),' +
        ' with wb value: (.+), with v_addr: (.+)')

def extract(p, line):
    m = p.match(line)
    if m:
        # print(m.group(1), m.group(2))
        return m.group(1), m.group(2)
    else:
        return None

with open(sys.argv[1]) as f, open(sys.argv[2]) as golden_f:
    count = 0
    exp_start_c = None
    base_start_c = None
    last_diff = 0
    for lt, lg in zip(f, golden_f):
        count += 1
        exp = extract(pattern, lt)
        base = extract(pattern, lg)
        if exp and base:
            c, n = exp
            bc, bn = base
            assert n == bn
            c, bc = int(c), int(bc)

            if exp_start_c is None:
                exp_start_c = c
                base_start_c = bc
                continue

            x, y = c - exp_start_c, bc - base_start_c
            diff = x - y

            # if diff // 500 > 1000 or (diff - last_diff) // 500 > 180:
            if diff // 500 > 10:
                print(diff // 500)
                print((diff - last_diff) // 500)
                print(lt)
                print(lg)
                break
            last_diff = diff

