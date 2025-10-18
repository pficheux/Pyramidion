CFLAGS= -O2  #-DUSE_MOSQUITTO # -Wall

PROGS= gpioIrq gpio_test

all: $(PROGS)

.c:
	$(CC) $(CFLAGS) -o $@ $<  -lpthread # -lmosquitto

clean:
	rm -f *~  $(PROGS)

install: $(PROGS)
	cp $(PROGS) $(DESTDIR)/usr/local/bin
