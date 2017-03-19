#!/bin/bash

for raw in $(ls /tmp/test_libvoxin*raw); do
    echo "--> $raw"
    wav=$(echo $raw | sed 's/raw/wav/')
    sox -r 11025 -e signed -b 16 -c 1 $raw $wav; aplay $wav
done
