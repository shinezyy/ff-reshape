. ./sync.sh
while true;
do
    date '+%Y.%m.%d_%H:%M:%S'
    push_source_scripts$2 135 /work/ff;
    if [ $1 -eq 0 ];
    then
        break
    fi
    sleep 60;
done
