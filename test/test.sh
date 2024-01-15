#! /bin/bash

# $1 - utility
# $2 - port
# $3 - path
# $4 - concurrency
# $5 - max requests
# $6 - intervals

port=$2
url="http://127.0.0.1:${port}$3"
name="nginx"

concurrency=$4
start=$4
end=$5
step=$(( ($end - $start) / $6 ))

n=$7

source ./extract/"$1.extract.sh"
extractor=$(extract)

file=./${name}/${concurrency}

mkdir -p ./${name}
rm -f "$file"
echo -n "req," > "$file"

for (( i = 0; n > i; i++ ));
do
    if [ $(( $n - 1 )) -eq $i ]; then
        echo "$i" >> "$file"
    else
        echo -n "$i," >> "$file"
    fi
done;

while [ ${start} -le ${end} ]
do
    echo "Start ${start} requests"
    echo -n "${start}," >> "$file"

    for (( i = 0; n > i; i++ ));
    do
        echo "Pass $i"
        t=$(eval "./run_single.sh $1 ${start} ${concurrency} \"${url}\" | $extractor")

        if [ $(( $n - 1 )) -eq $i ]; then
            echo "${t}" >> "$file"
        else
            echo -n "${t}," >> "$file"
        fi

    done;

    # t=$(eval "./run_single.sh $1 ${start} ${concurrency} \"${url}\" | $extractor")
    # echo "${start},${t}" >> "$file"
    # echo "N: ${start}; i: ${t}"
    echo "Finish ${start} requests"
    start=$((start + step))
done

echo "Finish"

