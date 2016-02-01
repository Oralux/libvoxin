BIN := api.o libvoxin.o
CFLAGS += $(DEBUG) -fPIC -I../api -I../common
CC ?= gcc
LIBDIR ?= ../../build/rfs/usr/lib/x86_64-linux-gnu
INSTALL_DIR ?= .
STRIP ?= strip --strip-unneeded

all: $(BIN)
	$(CC) -shared -Wl,-soname,libvoxin.so.1 -o libvoxin.so.1.0.0 -Wl,--version-script=libvoxin.ld $(^) $(LDFLAGS) -L$(LIBDIR) -lcommon
	$(STRIP) libvoxin.so.1.0.0

clean:
	rm -f *o *~ libvoxin.so*

install:
	install -m 644 libvoxin.so.1.0.0 $(INSTALL_DIR)
	cd $(INSTALL_DIR); ln -s libvoxin.so.1.0.0 libvoxin.so; ln -s libvoxin.so.1.0.0 libvoxin.so.1 
