#!/bin/bash

pkill speech
echo 2 > /tmp/libvoxin.ok
rm /tmp/libvoxin.log*

spd-say "hello everybody, how are you?"
