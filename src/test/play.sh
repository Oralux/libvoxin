#!/bin/bash

FREQ=11025
[ -n "$1" ] && FREQ=$1

for raw in $(ls /tmp/test_libvoxin*raw); do
    echo "--> $raw"
    wav=$(echo $raw | sed 's/raw/wav/')
    sox -r "$FREQ" -e signed -b 16 -c 1 $raw $wav; aplay $wav
done
