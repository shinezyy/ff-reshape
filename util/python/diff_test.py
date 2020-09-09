import re
import sys
import os
import os.path as osp
from multiprocessing import Pool

num_thread = 100

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

def compare_files(test, gold, outf):
    with open(test) as f, open(gold) as golden_f:
        count = 0
        for lt, lg in zip(f, golden_f):
            count += 1
            if count % 10000 == 0:
                print(count, file=outf)

            mt = match_and_append(pattern, lt)
            mg = match_and_append(pattern, lg)
            if mt is None and mg is None:
                print('.', file=outf)
                continue
            elif mt is None or mg is None:
                print(f"Difference found in line {count}, one is None!", file=outf)

            pc, val, addr = mt
            pcg, valg, addrg = mg

            if pc != pcg or val != valg:
                if val == '1' and valg == '3':
                    print('Ignore csrrs error', file=outf)
                else:
                    print(f"Difference found in line {count}", file=outf)
                    print("test:", pc, val, "gold:", pcg, valg, "addr:", addr, file=outf)
                    print("test:", lt, "gold:", lg, "addr:", addrg, file=outf)
                    break

def compare_wrapper(l):
    test, gold, fname = l
    with open(fname, 'w') as f:
        try:
            compare_files(test, gold, f)
        except Exception as e:
            print(e)
    return

def main():
    test = sys.argv[1]
    gold = sys.argv[2]
    if osp.isfile(test):
        assert(osp.isfile(gold))
        compare_files(test, gold, sys.stdout)
    else:
        assert(osp.isdir(test))

        if not osp.exists('diff'):
            os.makedirs('diff')

        log_file = 'gem5_out.txt'
        for d in os.listdir(test):
            print(osp.join(test, d, log_file))
            assert osp.isfile(osp.join(test, d, log_file))
            assert osp.isfile(osp.join(gold, d, log_file))

        p = Pool(num_thread)
        args = []
        for d in os.listdir(test):
            args.append([osp.join(test, d, log_file), osp.join(gold, d, log_file),
                    osp.join('diff', d)])
        p.map(compare_wrapper, args)


if __name__ == '__main__':
    main()
