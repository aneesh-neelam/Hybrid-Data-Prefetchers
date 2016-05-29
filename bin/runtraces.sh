#!/bin/sh

# The MIT License (MIT)
#
# Copyright (c) 2016 Aneesh Neelam
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

echo "default"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid using default setting"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid using default setting"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat
done


echo "\n\n\n\n\nsmall_llc"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid using small_llc setting"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -small_llc
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid using small_llc setting"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -small_llc
done


echo "\n\n\n\n\nlow_bandwidth"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid using low_bandwidth setting"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -low_bandwidth
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid using low_bandwidth setting"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -low_bandwidth
done


echo "\n\n\n\n\nscrambled_loads"
echo "VLDP AMPM Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on VLDP AMPM Hybrid using scramble_loads setting"
    zcat "$f" | ./bin/vldp_ampm -hide_heartbeat -scramble_loads
done

echo "\n\n\nDCPT VLDP Hybrid\n"
for f in ./traces/*.dpc.gz; do
    echo "Running trace: $f on DCPT VLDP Hybrid using scramble_loads setting"
    zcat "$f" | ./bin/dcpt_vldp -hide_heartbeat -scramble_loads
done
