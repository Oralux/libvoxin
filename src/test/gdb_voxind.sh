#!/bin/bash
NBARGS=$#

if [ "$NBARGS" != "1" ]; then
    echo "usage: $0 <test_number>"
    exit 1
fi

TEST_NUMBER=$1

touch /tmp/test_voxind
./run.sh $TEST_NUMBER &

set -v
# cd ../../build/rfs/usr/bin
# export LD_LIBRARY_PATH=../lib/x86_64-linux-gnu
# sudo gdb -p $(pidof voxind)
# rm /tmp/test_voxind
