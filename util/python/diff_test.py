import re

golden = []
test = []
pattern = re.compile('@\d+ Committing instruction with sn:(\d+) PC:\((.+=>.+)\)\.\(0=>1\), with wb value: (.+)')

def match_and_append(p, l, line):
    m = p.match(line)
    if m:
        # print(m.group(1), m.group(2))
        l.append((m.group(2), m.group(3)))

with open('./gem5_out.txt') as f, open('./cmp_gem5_out.txt') as golden_f:
    for line in f:
        match_and_append(pattern, test, line)
    for line in golden_f:
        match_and_append(pattern, golden, line)
golden = golden[0: len(test)]

count = 1
for t, g in zip (test, golden):
    if t[0] != g[0] or t[1] != g[1]:
        print(f"Difference found in line {count+31}")
        print("test:", t[0], t[1], "gold:", g[0], g[1])
        break
    count += 1

