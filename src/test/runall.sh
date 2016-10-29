#!/bin/bash
i=1
while [ "$i" -le "7" ]; do
    ./run.sh $i
    i=$((i+1))
done
