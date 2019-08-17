import re

golden = []
test = []
pattern = re.compile('@\d+ Committing instruction with sn:(\d+) PC:\((.+=>.+)\)\.\(0=>1\), with wb value: (.+)')

def match_and_append(p, line):
    m = p.match(line)
    if m:
        # print(m.group(1), m.group(2))
        return m.group(2), m.group(3)
    else:
        return None

with open('./gem5_out.txt.1') as f, open('./cmp_gem5_out.txt') as golden_f:
    count = 0
    for lt, lg in zip(f, golden_f):
        count += 1
        mt = match_and_append(pattern, lt)
        mg = match_and_append(pattern, lg)
        if mt is None and mg is None:
            continue
        elif mt is None or mg is None:
            print(f"Difference found in line {count}, one is None!")

        pc, val = mt
        pcg, valg = mg

        if pc != pcg or val != valg:
            print(f"Difference found in line {count}")
            print("test:", pc, val, "gold:", pcg, valg)
            print("test:", lt, "gold:", lg)
            break

        if count % 10000 == 0:
            print(count)

