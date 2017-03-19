#!/bin/bash
i=1
while [ "$i" -le "8" ]; do
    ./run.sh $i
    i=$((i+1))
done
