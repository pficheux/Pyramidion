CFLAGS= -O2 # -Wall

PROGS= gpioIrq

all: $(PROGS)

.c:
	$(CC) $(CFLAGS) -o $@ $<  -lmosquitto -lpthread

clean:
	rm -f *~  $(PROGS)

install: $(PROGS)
	cp $(PROGS) $(DESTDIR)/usr/local/bin
