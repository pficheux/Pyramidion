#!/bin/sh
#set -x

MQTT_SERVER=iot.eclipse.org
MQTT_TOPIC=pyramidion-test
GPIO_NR=21
BPM_O=30

GPIO_DIR=/sys/class/gpio
RPI_GPIO=rpi_gpio


# GPIO
gpio_off ()
{
    echo 0 > $GPIO_DIR/gpio${1}/value
}

init_gpio ()
{
    echo $1 > $GPIO_DIR/export
    echo out > $GPIO_DIR/gpio${1}/direction
    echo 0 > $GPIO_DIR/gpio${1}/value
    gpio_off ${1}
}

kill_program ()
{
    echo "killing $RPI_GPIO"
    killall $RPI_GPIO
    sleep 1
    gpio_off $GPIO_NR
}


# Convert BPM to ms
# 
get_period_value ()
{
    echo "60000/${1}/2" | bc -l | cut -d '.' -f 1
}    

# Exit function
do_exit ()
{
    kill_program

    exit 0
}

init_gpio $GPIO_NR

trap do_exit 2 3 15

# Start with 30 bpm
PERIOD=$(get_period_value $BPM_O)
$RPI_GPIO -g $GPIO_NR -p ${PERIOD}000000 -q &

while [ 1 ]
do
    BPM=$(mosquitto_sub -C 1 -h $MQTT_SERVER -t $MQTT_TOPIC)
    PERIOD=$(get_period_value $BPM)
    
    echo "received BPM is $BPM pulse/mn, period is $PERIOD ms"

    # if new value then kill program
    if [ $BPM -ne $BPM_O ]; then
	kill_program

	echo "starting $RPI_GPIO"
	$RPI_GPIO -g $GPIO_NR -p ${PERIOD}000000 -q &
    fi	

    BPM_O=$BPM
done
