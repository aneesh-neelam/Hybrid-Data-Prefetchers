#!/bin/sh

echo "default"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat
done


echo "\n\n\n\n\nsmall_llc"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -small_llc
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -small_llc
done


echo "\n\n\n\n\nlow_bandwidth"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -low_bandwidth
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -low_bandwidth
done


echo "\n\n\n\n\nscrambled_loads"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -scramble_loads
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -scramble_loads
done
