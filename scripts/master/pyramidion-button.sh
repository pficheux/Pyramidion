#!/bin/sh
#set -x

GPIO_IN=16
GPIO_SERV=pyramidion-gpio.service
MQTT_SERVER=iot.eclipse.org 
MQTT_TOPIC=pyramidion-test
MODE=~pi/pyramidion.mode

# init GPIO
echo $GPIO_IN > /sys/class/gpio/export
echo in > /sys/class/gpio/gpio${GPIO_IN}/direction

# Init crontab
echo "auto" > $MODE
crontab -u pi ~pi/cron.tab

# Check manual/auto button
while [ 1 ]
do
    # cron mode + active GPIO service -> do nothing
    if [ "$(cat $MODE)" = "auto" -a "$(systemctl is-active $GPIO_SERV)" = "active" ]; then
      echo "running in auto/cron mode"
    else
      # Auto -> stop GPIO if not auto
      if [ $(cat /sys/class/gpio/gpio${GPIO_IN}/value) -eq 1 ]; then
        # Manual -> stop GPIO + back to auto
	if [ "$(systemctl is-active $GPIO_SERV)" = "active" ]; then
	  echo "auto" > $MODE
    	  systemctl stop $GPIO_SERV
        fi
      else
	# Manual -> start GPIO
	if [ "$(systemctl is-active $GPIO_SERV)" != "active" ]; then
	  echo "manual" > ~pi/pyramidion.mode
	  systemctl start $GPIO_SERV 
        fi
      fi
    fi

    sleep 2
done
