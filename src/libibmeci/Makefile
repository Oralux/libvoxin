NAME=libibmeci.so

CFLAGS = -m32 -g -DDEBUG -fPIC -I../api -I../common -I/usr/i686-linux-gnu/include
LDFLAGS = -m32
CC = gcc
#STRIP = strip --strip-unneeded

all: main.o
	$(CC) -shared -o $(NAME) $(^) $(LDFLAGS) 

clean:
	rm -f *o *~

install:
	[ ! -d $(DESTDIR)/lib ] && mkdir -p $(DESTDIR)/lib || true
	install -m 644 $(NAME) $(DESTDIR)/lib

