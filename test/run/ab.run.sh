#! /bin/bash

function format {
    echo ab -n $1 -c $2 -r -l \"$3\" "2>&1"
}

