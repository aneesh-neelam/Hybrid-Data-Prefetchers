#!/bin/sh

echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat
done

echo "DCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat
done
