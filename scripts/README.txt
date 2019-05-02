Maître
======

master/

  cron.tab			Table CRON (arrêt/démarrage service GPIO)

  pyramidion-gpio.service	config systemd pour pilotage n GPIO (capteur+led)
  pyramidion-gpio.sh		script-shell appelé par le service systemd

  pyramidion-button.service	config systemd pour test button auto/manuel
  pyramidion-button.sh		script-shell appelé par le service systemd

Esclave
=======

slave/

  pyramidion-receive.service	config systemd pour le service esclave
  pyramidion-receive.sh		script esclave
  test_slave.sh			test en boucle du script de réception

  pyramidion-30bpm.sh		script 30 bpm initial (inutile)
  pyramidion-30bpm.service	config systemd pour le service 30bpm


