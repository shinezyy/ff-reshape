import sys

assert len(sys.argv) == 3

with open(sys.argv[1]) as f1, open(sys.argv[2]) as f2:
    for left, right in zip(f1, f2):
        if left != right:
            print('<<<\n', left, '--------')
            print(right, '>>>>\n')
