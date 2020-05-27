sync_source_only () {
    echo "syncing to $1:$2"
    rsync -arzv --update \
        --exclude=cmake-build-debug \
        --exclude=.idea \
        --exclude='*.pyc' \
        --exclude='__pycache__' \
        --exclude=build \
        --exclude='*.swp'\
        --exclude=SConstruct \
        --exclude=util/run_sh_scrpits/path_config.py \
        --exclude='parsetab.py' \
        --no-owner --no-group \
        --copy-unsafe-links \
        --exclude=.git \
        --exclude='.git*' \
        --exclude=util/run_sh_scrpits \
        ./ \
        $1:$2

}

sync_source_scripts () {
    echo "syncing to $1:$2"
    rsync -arzv --update \
        --exclude=cmake-build-debug \
        --exclude=.idea \
        --exclude='*.pyc' \
        --exclude='__pycache__' \
        --exclude=build \
        --exclude='*.swp'\
        --exclude=SConstruct \
        --exclude=util/run_sh_scrpits/path_config.py \
        --exclude='parsetab.py' \
        --no-owner --no-group \
        --copy-unsafe-links \
        --exclude=.git \
        --exclude='.git*' \
        ./ \
        $1:$2
}

sync_source_scripts_git () {
    echo "syncing to $1:$2"
    rsync -arzv --update \
        --exclude=cmake-build-debug \
        --exclude=.idea \
        --exclude='*.pyc' \
        --exclude='__pycache__' \
        --exclude=build \
        --exclude='*.swp'\
        --exclude=SConstruct \
        --exclude=util/run_sh_scrpits/path_config.py \
        --exclude='parsetab.py' \
        --no-owner --no-group \
        --copy-unsafe-links \
        ./ \
        $1:$2
}

sync_source_git () {
    echo "syncing to $1:$2"
    rsync -arzv --update \
        --exclude=cmake-build-debug \
        --exclude=.idea \
        --exclude='*.pyc' \
        --exclude='__pycache__' \
        --exclude=build \
        --exclude='*.swp'\
        --exclude=SConstruct \
        --exclude=util/run_sh_scrpits/path_config.py \
        --exclude='parsetab.py' \
        --no-owner --no-group \
        --copy-unsafe-links \
        --exclude=util/run_sh_scrpits \
        ./ \
        $1:$2
}

