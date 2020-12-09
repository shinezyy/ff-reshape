from spec_common import Spec17Commands
from spec_common import Spec06Commands
import os.path as osp
import sh
import os


cmds = dict()

ver = '17'
if ver == '06':
    spec06 = Spec06Commands()
    raw = spec06.get_all()
else:
    spec17 = Spec17Commands()
    raw = spec17.get_all()

script_template_1 = '''#!/bin/sh

echo "Mounting /ramfs"
mount -t tmpfs tmpfs /ramfs

echo "Linking input files"

echo "CD to ramfs"
mkdir /ramfs/{bname}
cd /ramfs/{bname}

# copy input files
'''

script_template_2 ='''
echo "===== Start running SPEC20{ver} ref ====="

# notify processor start spec command
# /root/blabla

echo "running command"
# run command
/root/spec{ver}_exe/{task}

# good trap: stop processor
/root/trap
'''

copy_io_files = {
        'common': '''
cp -r /root/cpu20{ver}_run_dir/{bname}/* /ramfs/{bname}
            ''',

        '17': {
            'x264_pass1': '''
ln -sf /root/cpu2017_run_dir/x264/BuckBunny.yuv .
cp -n -r /root/cpu2017_run_dir/x264/* .
ls -alt
            ''',
            'x264_pass2': '''
ln -sf /root/cpu2017_run_dir/x264/BuckBunny.yuv .
cp -n -r /root/cpu2017_run_dir/x264_pass2/* .
cp -n -r /root/cpu2017_run_dir/x264/* .
ls -alt
            ''',
            'x264_seek': '''
ln -sf /root/cpu2017_run_dir/x264/BuckBunny.yuv .
cp -n -r /root/cpu2017_run_dir/x264_seek/* .
cp -n -r /root/cpu2017_run_dir/x264/* .
ls -alt
            ''',
            },
        '06': {
            },
        }

def preprocess():
    for k in raw:
        cmd = raw[k]['cmd']
        binary = cmd[0]
        if ver == '17':
            binary_with_suffix = binary + '_base.riscv64-linux-gnu-gcc-9.3.0-64' # for 2017
        else:
            binary_with_suffix = binary + '_base.riscv64-linux-gnu-gcc-9.3.0' # 2006
        cmd[0] = binary_with_suffix

        cmds[k] = cmd

def gen_scripts():
    for k in cmds:
        cmd = cmds[k]
        benchmark_name = k.split('_')[0]

        copy_script = copy_io_files['common']
        if k in copy_io_files[ver]:
            copy_script = copy_io_files[ver][k]

        script_template = script_template_1 + copy_script + script_template_2

        x = script_template.format(**{
            'bname': benchmark_name,
            'ver': ver,
            'task': ' '.join(cmd),
            })
        print(x)
        sdcard_path = "/home/zyy/projects/NEMU/sdcard-6g/"
        script_path = osp.join(sdcard_path, f"root/spec{ver}_scripts/run-{k}.sh")
        with open(script_path, 'w') as f:
            f.write(x)
        sh.chmod(['+x', script_path])

def copy_to(src, dest):
    if osp.isdir(src):
        for f in os.listdir(src):
            sh.cp(['-r', osp.join(src, f), dest])

def copy_inputs():
    for k in raw:
        name = k.split('_')[0]

        if ver == '06':
            ref_inputs = '/home/zyy/research-data/spec2006/benchspec/CPU2006/{}/data/ref/input/'
            all_inputs = '/home/zyy/research-data/spec2006/benchspec/CPU2006/{}/data/all/input/'

            dest = f'/home/zyy/research-data/cpu2006_run_dir_clean/{name}'
        else:
            ref_inputs = '/home/zyy/research-data/spec2017_20201126/benchspec/CPU/{}/data/refrate/input/'
            all_inputs = '/home/zyy/research-data/spec2017_20201126/benchspec/CPU/{}/data/all/input/'

            dest = f'/home/zyy/research-data/cpu2017_run_dir_clean/{name}'

        ref_inputs = ref_inputs.format(raw[k]['id'])
        all_inputs = all_inputs.format(raw[k]['id'])
        if not osp.exists(dest):
            os.makedirs(dest)

        copy_to(ref_inputs, dest)
        copy_to(all_inputs, dest)

def gather_binary():
    if ver == '06':
        bin_template = '/home/zyy/research-data/spec2006/benchspec/CPU2006/{}/exe/{}' # for 06
    else:
        bin_template = '/home/zyy/research-data/spec2017_20201126/benchspec/CPU/{}/exe/{}' # for 17
    for k in raw:
        bin_path = bin_template.format(raw[k]['id'], cmds[k][0])
        print(bin_path)
        assert osp.isfile(bin_path)
        sh.cp([bin_path, './spec{ver}_exe'])


preprocess()
# copy_inputs()
# gather_binary()
gen_scripts()

