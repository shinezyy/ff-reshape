from spec_common import Spec17Commands
import os.path as osp
import sh

spec17 = Spec17Commands()

raw = spec17.get_all()
cmds = dict()

script_template = '''

mount -t tmpfs tmpfs /ramfs

# copy input files
cp -r /root/cpu2017_run_dir/{} /ramfs

cd /ramfs/{}

echo "===== Start running SPEC-int test ====="

# notify processor start spec command
/root/blabla

# run command
/root/spec17_exe/{}

# good trap: stop processor
/root/trap
'''

def preprocess():
    for k in raw:
        cmd = raw[k]['cmd']
        binary = cmd[0]
        binary_with_suffix = binary + '_base.riscv64-linux-gnu-gcc-9.3.0-64'
        cmd[0] = binary_with_suffix

        cmds[k] = cmd

def gen_scripts():
    for k in cmds:
        cmd = cmds[k]
        benchmark_name = k.split('_')[0]
        x = script_template.format(benchmark_name, benchmark_name, ' '.join(cmd))
        # sudo cp x /sdcard/sbin/
        # sudo pack image to f'{k}.img.gz'


def gather_binary():
    bin_template = '/home/zyy/research-data/spec2017_20201126/benchspec/CPU/{}/exe/{}'
    for k in raw:
        bin_path = bin_template.format(raw[k]['id'], cmds[k][0])
        assert osp.isfile(bin_path)
        print(bin_path)
        sh.cp([bin_path, './spec_exe'])


preprocess()
# gather_binary()
gen_scripts()

