#! /bin/bash

if [ "l" == "$1" ]; then
    ls -l run | tail -n +2 | awk '{print $9}' | sed 's/\.run\.sh//'
else
    source ./run/"$1.run.sh"
    func=$(format $2 $3 $4)
    echo $func
    eval $func
fi

