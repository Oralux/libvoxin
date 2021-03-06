#!/bin/bash

FREQ=11025
[ -n "$1" ] && FREQ=$1

for raw in $(ls /tmp/test_libvoxin*raw); do
    echo "--> $raw"
	aplay -c 1 -f S16_LE -r $FREQ $raw
done

#sox -t raw -r $FREQ -c 1 -b 16 -e signed $raw  ${raw%.raw}.wav
