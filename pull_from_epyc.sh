. ./sync.sh
while true;
do
    date '+%Y.%m.%d_%H:%M:%S'
    pull_source_scripts$2 epyc-1 ~/projects/omegaflow;
    if [ $1 -eq 0 ];
    then
        break
    fi
    sleep 60;
done
