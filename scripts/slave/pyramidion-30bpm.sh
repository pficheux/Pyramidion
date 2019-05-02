#!/bin/sh
#set -x

# FIXME: this should be done in pyramidion-receive.sh !

GPIO_NR=21
GPIO_DIR=/sys/class/gpio
RPI_GPIO=rpi_gpio_ns

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

init_gpio $GPIO_NR
$RPI_GPIO -g $GPIO_NR -p 1000000000 -q
