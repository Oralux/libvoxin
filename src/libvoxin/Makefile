SONAME=libvoxin.so.1
MIN=2
REV=0

BIN := api.o libvoxin.o
CFLAGS += $(DEBUG) -fPIC -I../api -I../common -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
CC ?= gcc
LIBDIR ?= ../../build/rfs/usr/lib/x86_64-linux-gnu
INSTALL_DIR ?= .
STRIP ?= strip --strip-unneeded

all: $(BIN)
	$(CC) -shared -Wl,-soname,libvoxin.so.1 -o $(SONAME).$(MIN).$(REV) -Wl,--version-script=libvoxin.ld $(^) $(LDFLAGS) -L$(LIBDIR) -lcommon
	$(STRIP) $(SONAME).$(MIN).$(REV)

clean:
	rm -f *o *~ libvoxin.so*

install:
	install -m 644 $(SONAME).$(MIN).$(REV) $(INSTALL_DIR)
	cd $(INSTALL_DIR); ln -s $(SONAME).$(MIN).$(REV) libvoxin.so; ln -s $(SONAME).$(MIN).$(REV) $(SONAME)
