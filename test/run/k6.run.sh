#! /bin/bash

function format {
    echo k6 run --vus $2 --iterations $1 -e HOST=\"$3\" k6.run.js
}

