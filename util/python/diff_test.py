import re
import sys
import os
import os.path as osp
from multiprocessing import Pool
import argparse

num_thread = 100

golden = []
test = []
pattern = re.compile('\d+ VCommitting (\d+) instruction with sn:(\d+) PC:\((.+=>.+)\)\.\(0=>1\),' +
        ' with wb value: (.+), with v_addr: (.+)')

def match_and_append(p, line):
    m = p.match(line)
    if m:
        return m.group(3), m.group(4), m.group(5)
    else:
        return None


def match_and_find_icount(p, line):
    m = p.match(line)
    if m:
        return m.group(1), m.group(3), m.group(4), m.group(5)
    else:
        return None


def find_next_valid_line(f):
    count = 0
    while True:
        count += 1
        line = f.readline()
        if line is None:
            return None

        if pattern.match(line):
            return count, line


def find_until_seq(f, seq):
    count = 0
    print(f'Heading to {seq}')
    while True:
        count += 1
        line = f.readline()
        if line is None:
            return None

        m = pattern.match(line)
        if m is not None and m.group(1) == seq:
            print('##')
            return count, m.group(1), m.group(3), m.group(4), m.group(5)

        if count % 10000 == 0:
            print(count)


def nr_compare_files(test, gold, outf):
    with open(test) as f, open(gold) as golden_f:
        t_count = 0
        g_count = 0
        while True:
            tc, lt = find_next_valid_line(f)
            gc, lg = find_next_valid_line(golden_f)
            t_count += tc
            g_count += gc
            mt = match_and_find_icount(pattern, lt)
            mg = match_and_find_icount(pattern, lg)

            if mt is None or mg is None:
                print(f"Difference found in test line {t_count}, gold line {g_count}, one is None!", file=outf)

            t_seq, pc, val, addr = mt
            g_seq, pcg, valg, addrg = mg

            if t_seq > g_seq:
                gc, g_seq, pcg, valg, addrg = find_until_seq(golden_f, t_seq)
                print(g_seq, pcg, valg, addrg)
                g_count += gc

            elif t_seq < g_seq:
                tc, t_seq, pc, val, addr = find_until_seq(t, g_seq)
                t_count += tc


            if pc != pcg or val != valg:
                if val == '1' and (valg == '3' or valg == '0'):
                    print('Ignore csrrs error', file=outf)
                else:
                    print(f"Difference found in test line {t_count}, gold line {g_count}", file=outf)
                    print(f"test\tseq: {t_seq}\tpc: {pc}\tval: {val}\taddr: {addr}", file=outf)
                    print(f"gold\tpc: {g_seq}\tpc: {pcg}\tval: {valg}\taddr: {addrg}", file=outf)
                    # print("test:", lt, "gold:", lg, "addr:", addrg, file=outf)
                    break
            if t_count % 10000 == 0:
                print(t_count)


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
                if val == '1' and (valg == '3' or valg == '0'):
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
    parser = argparse.ArgumentParser()
    parser.add_argument('-g', '--gold', type=str, required=True)
    parser.add_argument('-t', '--test', type=str, required=True)

    parser.add_argument('-r', '--noise-resistant', action='store_true')

    args = parser.parse_args()

    test = args.test
    gold = args.gold

    if osp.isfile(test):
        assert(osp.isfile(gold))
        if args.noise_resistant:
            nr_compare_files(test, gold, sys.stdout)
        else:
            compare_files(test, gold, sys.stdout)
    else:
        assert(osp.isdir(test))
        assert not args.noise_resistant

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
