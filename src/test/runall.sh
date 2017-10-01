#!/bin/bash
i=1
while [ "$i" -le "9" ]; do
    ./run.sh $@ $i
    i=$((i+1))
done
