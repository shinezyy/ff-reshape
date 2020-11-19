rm -i $1;
ssh -t epyc-1 rm -i ~/projects/omegaflow/$1;
ssh -t D13-RT rm -i /home/zyy-work/ff/$1;
# ssh -t tokyo2x rm -i ~/projects/omegaflow/$1
