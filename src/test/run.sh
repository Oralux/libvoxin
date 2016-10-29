#!/bin/bash

NBARGS=$#

if [ "$NBARGS" != "1" ]; then
    echo "usage: $0 <test_number>"
    exit 1
fi

TEST_NUMBER=$1

rm -f /tmp/libvoxin.log*
echo 2 > /tmp/libvoxin.ok
cd ../../build/rfs/usr/bin
ln -sf test$TEST_NUMBER client
export LD_LIBRARY_PATH=../lib/x86_64-linux-gnu
export PATH=.:$PATH
./client
RES=$?
if [ "$RES" = "0" ]; then
    # sox -r 11025 -e signed -b 16 -c 1 /tmp/test_libvoxin.raw /tmp/test_libvoxin.wav; aplay /tmp/test_libvoxin.wav
    echo -e "$TEST_NUMBER\t[OK]"
else
    echo -e "$TEST_NUMBER\t[KO] ($RES)"
fi
# ls /tmp/libvoxin.log.*

exit $RES
