f="$(basename -- $1)"
cd o3
git diff HEAD~2..HEAD $f > $f.patch
cd ..
cd forwardflow
if [ $# -eq 1 ]
then
    patch -p1 $f ../o3/$f.patch
else
    f2="$(basename -- $2)"
    patch -p1 $f2 ../o3/$f.patch
fi