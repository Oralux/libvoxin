#!/bin/bash

unset PLAY
which aplay && PLAY=aplay
which paplay && PLAY=paplay

[ -z "$PLAY" ] && echo "Install aplay (alsa-utils) or paplay (pulseaudio-utils)" && exit 1

./say | $PLAY

