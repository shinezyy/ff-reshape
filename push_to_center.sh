. ./sync.sh
while true;
do
    push_source_scripts$2 tokyo2 ~/projects/omegaflow;
    if [ $1 -eq 0 ];
    then
        break
    fi
    sleep 5;
done
