#!/bin/sh

for f in ./traces/*.dpc.gz; do
    zcat "$f" | ./bin/dpc2sim
done

