#!/bin/sh

for f in ./traces/*.dpc.gz; do
    zcat "$f" | ./bin/vldp_ampm_dpc2sim
done
