mkdir bc
TEST_DIR="assign3-test0_29"
cd $TEST_DIR
cfiles=$(ls .)
for file in $cfiles; do
    prefix=${file:0:6}
    bc_file="$prefix.bc"
    clang -emit-llvm -c -O0 -g3 $file -o $bc_file
    # llvm-dis $bc_file
done
cd ..
mv $TEST_DIR/*.bc bc
# mv assign2-tests/*.ll bc