#!/bin/bash

#cat ../../doc/pg135.txt | head -n 1000 | tail -n $((1000-620)) > /tmp/test.txt
#./say -f /tmp/test.txt  -w /tmp/test.raw
./say -w /tmp/test.raw
sox -r 11025 -e signed -b 16 -c 1 /tmp/test.raw /tmp/test.wav
