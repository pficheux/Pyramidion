CFLAGS= -O2

PROG= rpi_gpio

OBJS= $(PROG).o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS) -lrt 

clean:
	rm -f *~ $(OBJS)  $(PROG)

install: $(PROG)
	cp $(PROG) $(DESTDIR)/usr/local/bin
