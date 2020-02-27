# echo "syncing to 128"
# rsync -arzv --update \
#     --exclude='*.pyc' \
#     --exclude='__pycache__' \
#     --exclude='*.swp'\
#     --no-owner --no-group \
#     --copy-unsafe-links \
#     ./src \
#     128-rt:/home/zyy/projects/gem5

echo "syncing to 135"
rsync -arzv --update \
    --exclude='*.pyc' \
    --exclude='__pycache__' \
    --exclude='*.swp'\
    --no-owner --no-group \
    --copy-unsafe-links \
    ./src \
    135-rt:/work/ff

# echo "syncing to dell"
# rsync -arzv --update \
#     --exclude='*.pyc' \
#     --exclude='__pycache__' \
#     --exclude='*.swp'\
#     --no-owner --no-group \
#     --copy-unsafe-links \
#     ./src \
#     dell-at-home:~/projects/omegaflow


