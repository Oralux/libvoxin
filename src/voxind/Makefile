BIN = main.o
CFLAGS = -ggdb -DDEBUG -m32 -I../common -I../api
LDFLAGS = -m32
CC ?= gcc
STRIP ?= strip --strip-unneeded
LIBDIR ?= ../../build/rfs/usr/lib/i386-linux-gnu

INSTALL_DIR ?= .

voxind: $(BIN)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lcommon /opt/IBM/ibmtts/lib/libibmeci.so
	$(STRIP) $(@)

all: voxind

clean:
	rm -f *o *~ voxind

install:
	install -m 755 voxind $(INSTALL_DIR)
