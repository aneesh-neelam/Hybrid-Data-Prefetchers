#!/bin/sh

echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    zcat "$f" | ./bin/vldp_ampm
done

echo "DCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    zcat "$f" | ./bin/dcpt_vldp
done
