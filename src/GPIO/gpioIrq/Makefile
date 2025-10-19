CFLAGS= -O2 -Wall #-DUSE_MOSQUITTO # -Wall

PROGS= gpioIrq gpioIrq_th gpio_test

all: $(PROGS)

.c:
	$(CC) $(CFLAGS) -o $@ $<  -lpthread # -lmosquitto

clean:
	rm -f *~  $(PROGS)

install: $(PROGS)
	cp $(PROGS) $(DESTDIR)/usr/local/bin
