echo "syncing to 128"
rsync -arzv --update \
    --exclude=cmake-build-debug \
    --exclude=.idea \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude=build \
    --exclude='*.swp'\
    --exclude=sync.sh \
    --exclude=util/run_sh_scrpits/common.py \
    --exclude='parsetab.py' \
    --no-owner --no-group \
    --copy-unsafe-links \
    ./ \
    128:/home/zyy/projects/gem5/
    # --exclude=.git \
    # --exclude='.git*' \
    # --exclude=util/run_sh_scrpits \

echo "syncing to 130"
rsync -arzv --update \
    --exclude=cmake-build-debug \
    --exclude=.idea \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude=build \
    --exclude='*.swp'\
    --exclude=sync.sh \
    --exclude=util/run_sh_scrpits/common.py \
    --exclude='parsetab.py' \
    --no-owner --no-group \
    --copy-unsafe-links \
    ./ \
    130:/home/zhouyaoyang/projects/ff/
    # --exclude=.git \
    # --exclude='.git*' \
    # --exclude=util/run_sh_scrpits \

echo "syncing to Dnode13"
rsync -arzv --update \
    --exclude=cmake-build-debug \
    --exclude=.idea \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude=build \
    --exclude='*.swp'\
    --exclude=sync.sh \
    --exclude=util/run_sh_scrpits/* \
    --exclude='parsetab.py' \
    --no-owner --no-group \
    --copy-unsafe-links \
    ./ \
    D13-RT:/home/zyy-work/ff/
    # --exclude=.git \
    # --exclude='.git*' \
    # --exclude=util/run_sh_scrpits \



echo "syncing to THU gold"
rsync -arzv --update \
    --exclude=cmake-build-debug \
    --exclude=.idea \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude=build \
    --exclude='*.swp'\
    --exclude=sync.sh \
    --no-owner --no-group \
    --copy-unsafe-links \
    --exclude=util/run_sh_scrpits/* \
    --exclude='parsetab.py' \
    ./ \
    fj-cpu:/home/auser/projects/ff/
    # --exclude=util/run_sh_scrpits \
    # --exclude=.git \
    # --exclude='.git*' \

# echo "syncing to Fuzhou gold"
# rsync -arzv --update \
#     --exclude=cmake-build-debug \
#     --exclude=.idea \
#     --exclude='*.pyc' \
#     --exclude='__pycache__' \
#     --exclude=build \
#     --exclude='*.swp'\
#     --exclude=sync.sh \
#     --no-owner --no-group \
#     --copy-unsafe-links \
#     --exclude=util/run_sh_scrpits/* \
#     --exclude='parsetab.py' \
#     ./ \
#     fuzhou-gold-28c-2.6GHz:/home/zyy/projects/ff/
#     # --exclude=util/run_sh_scrpits \
#     # --exclude=.git \
#     # --exclude='.git*' \

