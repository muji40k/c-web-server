#! /bin/bash

function extract {
    echo "grep \"Time taken for tests:\" | awk '{print \$5}'"
}

