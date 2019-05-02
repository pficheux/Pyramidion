#!/bin/sh
#set -x

GPIO_IN=20
GPIO_OUT=21
MQTT_SERVER=iot.eclipse.org
MQTT_TOPIC=pyramidion-test

gpioIrq -i $GPIO_IN -o $GPIO_OUT -h $MQTT_SERVER -t $MQTT_TOPIC
