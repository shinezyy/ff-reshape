. ./sync.sh
while true;
do
    push_source_scripts$2 135 /work/ff;
    if [ $1 -eq 0 ];
    then
        break
    fi
    sleep 5;
done
