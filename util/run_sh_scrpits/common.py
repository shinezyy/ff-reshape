import os
import sh
from os.path import join as pjoin
import os.path as osp
import local_config as lc


def avoid_repeated(func, outdir, func_id=None, binary=None, *args, **kwargs):
    assert func_id is not None
    func_name = func_id

    running_lock_name = 'running-{}'.format(func_name)
    running_lock_file = pjoin(outdir, running_lock_name)

    if os.path.isfile(running_lock_file):
        print('running lock found in {}, skip!'.format(outdir))
        return
    else:
        sh.touch(running_lock_file)

    ts_name = 'ts-{}'.format(func_name)
    out_ts_file = pjoin(outdir, ts_name)
    if os.path.isfile(out_ts_file):
        out_ts = os.path.getmtime(out_ts_file)
    else:
        out_ts = 0.0

    script_dir = os.path.dirname(os.path.realpath(__file__))
    script_ts_file = pjoin(script_dir, ts_name)
    if not os.path.isfile(script_ts_file):
        sh.touch(script_ts_file)

    newest = os.path.getmtime(script_ts_file)

    if not binary is None:
        binary_ts = os.path.getmtime(binary)
        newest = max(binary_ts, newest)

    if out_ts < newest:
        try:
            func(*args, **kwargs)
            sh.touch(out_ts_file)
        except Exception as e:
            print(e)
        sh.rm(running_lock_file)
    else:
        print('{} is older than {}, skip!'.format(out_ts_file,
            script_ts_file))
        if os.path.isfile(running_lock_file):
            sh.rm(running_lock_file)


def gem5_home():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    paths = script_dir.split('/')
    return '/'.join(paths[:-2])  # delete '/util/run_sh_scrpits'


def gem5_build(arch):
    return pjoin(gem5_home(), 'build/{}'.format(arch))


def gem5_exec(spec_version = '2006'):
    if spec_version == '2006':
        return os.environ['gem5_run_dir']
    elif spec_version == '2017':
        return os.environ['spec2017_run_dir']
    return None


def gem5_cpt_dir(arch, version=2006):
    return lc.cpt_dirs[version][arch]


def get_mem_demand(cpt_dir, cpt_id):
    mem_usage_file = f'mem_demand_cpt_0{cpt_id}'
    mem_usage_file = osp.join(cpt_dir, mem_usage_file)

    if osp.isfile(mem_usage_file):
        return open(mem_usage_file).read().strip()
    else:
        return None


class G5Config:
    def __init__(self, benchmark, bmk_outdir, cpt_id, arch,
                 full=True,
                 cpu_model='OoO',
                 window_size=192,
                 debug=False,
                 panic_tick=None,
                 debug_flags=None,
                 spec=2017,
                 func_id=None,
                 ):
        self.interval = 200 * 10 ** 6
        self.warmup = 20 * 10 ** 6
        self.benchmark = benchmark
        self.bmk_outdir = bmk_outdir
        self.cpt_id = cpt_id
        self.arch = arch
        self.bench_cpt_dir = pjoin(gem5_cpt_dir(self.arch, 2017), benchmark)
        self.mem_demand = get_mem_demand(self.bench_cpt_dir, cpt_id)
        self.debug = debug
        self.debug_flags = debug_flags
        self.spec_version = spec
        self.func_id = func_id
        self.task = ''

        if full:
            self.insts = 220 * 10 ** 6
        else:
            self.insts = 19 * 10 ** 6

        self.options = []
        self.post_options = []
        self.arch_options = []
        self.panic_tick = panic_tick
        self.cpu_model = cpu_model
        self.window_size = window_size
        self.configurable_options = dict()

        self.set_default_arch_options()

    def lazy_set_options(self):

        if self.debug and len(self.debug_flags):
            self.options += [
                    '--debug-flags={}'.format   (','.join(self.debug_flags)),
                    ]
            if self.panic_tick is not None:
                self.options += [
                        '--debug-start={}'.format   (self.panic_tick - 2000000),
                        '--debug-end={}'.format     (self.panic_tick + 2000000),
                        ]

        self.options += [
            '-q',
            '--stats-file=stats.txt',
            '--outdir=' + self.bmk_outdir,
            pjoin(gem5_home(), 'configs/spec2017/se_spec17.py'),
            '--spec-2017-bench',
            '-b', '{}'.format(self.benchmark),

            '--benchmark-stdout={}/out'.format(self.bmk_outdir),
            '--benchmark-stderr={}/err'.format(self.bmk_outdir),
            '-I {}'.format(self.insts),
            '-r {}'.format(self.cpt_id + 1),
            '--restore-simpoint-checkpoint',
            '--checkpoint-dir={}'.format(self.bench_cpt_dir),
            '--arch={}'.format(self.arch),
            '--spec-size=ref',
        ]

        self.options += self.arch_options
        if self.mem_demand is None:
            self.options.append('--mem-size=512MB')
        else:
            self.options.append(f'--mem-size={self.mem_demand}')

    def set_default_arch_options(self):
        self.configurable_options = {
            **self.configurable_options,
            '--num-ROB': self.window_size,
            '--num-PhysReg': self.window_size,
            '--num-IQ': round(0.43 * self.window_size),
            '--num-LQ': round(0.32 * self.window_size),
            '--num-SQ': round(0.25 * self.window_size),
            '--use-bp': 'ZPerceptron',
        }

        if self.cpu_model == 'TimingSimple':
            self.configurable_options = {
                **self.configurable_options,
                '--cpu-type': 'TimingSimpleCPU',
                '--mem-type': 'SimpleMemory',
            }
        elif self.cpu_model == 'OoO':
            self.arch_options += [
                '--caches',
                '--l2cache',
            ]
            self.configurable_options = {
                **self.configurable_options,
                '--cpu-type': 'DerivO3CPU',
                '--mem-type': 'DDR3_1600_8x8',

                '--cacheline_size': '64',

                '--l1i_size': '32kB',
                '--l1d_size': '32kB',
                '--l1i_assoc': '8',
                '--l1d_assoc': '8',

                '--l2_size': '4MB',
                '--l2_assoc': '8',
            }
        else:
            print("Unknown CPU type")
            assert False

    def add_options(self, options):
        self.post_options += options

    def update_options(self, options: dict):
        for option, value in options.items():
            self.configurable_options[option] = value

    def encode_options(self):
        s = []
        for option, value in self.configurable_options.items():
            s.append(f'{option}={value}')
        return s

    def run(self):
        self.lazy_set_options()
        self.options += self.encode_options()
        self.options += self.post_options
        # fix up branch outcome path
        if '--use-bp' in self.configurable_options and \
                self.configurable_options['--use-bp'] == 'OracleBP':
            o_path = pjoin(lc.branch_outcome_dir, self.task, "BranchTrace.branch.protobuf.gz")
            self.options += [
                    f'--outcome-path={o_path}',
                    ]
        gem5 = sh.Command(pjoin(gem5_build(self.arch), 'gem5.opt'))
        # sys.exit(0)
        gem5(
            _out=pjoin(self.bmk_outdir, 'gem5_out.txt'),
            _err=pjoin(self.bmk_outdir, 'gem5_err.txt'),
            *self.options
        )

    def check_and_run(self):
        self.task = "{}_{}".format(self.benchmark, self.cpt_id)

        if not os.path.isdir(self.bmk_outdir):
            os.makedirs(self.bmk_outdir)

        cpt_flag_file = pjoin(
            gem5_cpt_dir(self.arch, self.spec_version), self.benchmark,
            'ts-take_cpt_for_benchmark')
        prerequisite = os.path.isfile(cpt_flag_file)

        if prerequisite:
            print('cpt flag found, is going to run gem5 on', self.task)
            avoid_repeated(
                self.run, self.bmk_outdir,
                func_id=self.func_id,
                binary=pjoin(gem5_build(self.arch), 'gem5.opt'),
            )
        else:
            print('prerequisite not satisfied, abort on', self.task)


def run_wrapper(g5: G5Config):
    g5.check_and_run()


stats_base_dir = lc.stats_base_dir
