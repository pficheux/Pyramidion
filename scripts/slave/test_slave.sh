#!/bin/sh
set -x

BPM=40

while [ 1 ]
do
    mosquitto_pub -h iot.eclipse.org -t pyramidion-2 -m "$BPM"
    sleep 20
    BPM=$(expr $BPM + 20)
    if [ $BPM -gt 100 ]; then
	BPM=30
    fi
done    
