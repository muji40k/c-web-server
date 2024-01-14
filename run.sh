#! /bin/bash

sudo LD_PRELOAD=/usr/lib/libgcc_s.so.1 ./app.out ${@:1}

