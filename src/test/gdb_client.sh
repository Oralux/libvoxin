#!/bin/bash -vx
NBARGS=$#

if [ "$NBARGS" != "1" ]; then
    echo "usage: $0 <test_number>"
    exit 1
fi

TEST_NUMBER=$1

rm -f /tmp/test_libvoxin.dbg
rm -f /tmp/libvoxin.log*
echo 2 > /tmp/libvoxin.ok
cd ../../build/rfs/usr/bin
#ln -sf test$TEST_NUMBER client
export LD_LIBRARY_PATH=../lib/x86_64-linux-gnu
#gdb ./client
gdb ./test$TEST_NUMBER
