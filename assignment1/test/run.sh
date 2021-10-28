#!/bin/bash

#ast-interpreter "`cat $1`"


index=(00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24)

#for id in ${index[@]}
#do
#	echo running on test${id}.c:
#	ast-interpreter "`cat test${id}.c`"
#	echo
#done

result=(100 10 20 200 10 10 20 10 20 20 5 100 4 20 12 -8 30 10 10,20 10,20 5 11 42 24,42 720)

for((i=0;i<${#index[@]};i++))
do
    echo running on test${index[i]}.c
    echo acc = ${result[i]}
    ast-interpreter "`cat test${index[i]}.c`"
    echo
done
