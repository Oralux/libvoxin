#!/bin/bash

sox -r 11025 -e signed -b 16 -c 1 /tmp/test_libvoxin.raw /tmp/test_libvoxin.wav; aplay /tmp/test_libvoxin.wav
