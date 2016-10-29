BIN = main.o
CFLAGS = -ggdb -DDEBUG -m32 -I../common -I../api
LDFLAGS = -m32
CC ?= gcc
STRIP ?= strip --strip-unneeded
LIBDIR ?= ../../build/rfs/usr/lib/i386-linux-gnu
LIBDIR2 ?= /opt/IBM/ibmtts/lib

INSTALL_DIR ?= .

voxind: $(BIN)
	$(CC) -o $(@) $(^) -ggdb $(LDFLAGS) -L$(LIBDIR) -lcommon $(LIBDIR2)/libibmeci.so
	$(STRIP) $(@)

all: voxind

clean:
	rm -f *o *~ voxind

install:
	install -m 755 voxind $(INSTALL_DIR)
