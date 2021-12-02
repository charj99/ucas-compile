# build
mkdir build
cd build
cmake –DLLVM_DIR=/usr/local/llvm10d -DCMAKE_BUILD_TYPE=Debug ../.
make
cd ..

source sh/compile.sh # generate .bc
source sh/gen_truth.sh # gen ground-truth

# grading
ANALYSIS="build/assignment3"
BC_DIR="bc"
GROUND_TRUTH="ground-truth"
FORMATER_HELPER="sh/format_helper.py"

file_list=$(ls $BC_DIR)
total=0
correct=0
is_match=0

do_compare() {
    if [[ $is_match = 1 ]]; then
        return
    fi
    actual=$($ANALYSIS "$BC_DIR/$bc_file" 2>&1>/dev/null)
    expected=$(cat "$GROUND_TRUTH/${bc_file:0:6}$1")
    # format 
    actual=$(python $FORMATER_HELPER "$actual")
    expected=$(python $FORMATER_HELPER "$expected")
    
    [[ "$actual" == "$expected" ]] && is_match=1 || is_match=0
}

failed_str=""
for bc_file in $file_list; do
    if [[ "${bc_file:6:3}" = ".bc" ]]; then
        total=$(( $total + 1 ))
        file_id="${bc_file:4:2}"
        is_match=0
        do_compare ".txt"

        # if [[  $file_id = "08" ]] || [[  $file_id = "01" ]]; then
        #     do_compare ".txt.1"
        # fi
        if [[ $is_match == 1 ]]; then
                echo "$bc_file passed"
                correct=$(( $correct + 1 ))
        else
            echo "$bc_file failed:"
            echo "expected:"
            echo "$expected"
            echo "given:"
            echo "$actual"
            failed_str="$failed_str$file_id "
        fi
    fi
done
echo "$correct/$total"
echo "fail test cases:: $failed_str"