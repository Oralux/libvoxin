SONAME=libvoxin.so.1
MIN=1
REV=2

BIN := api.o libvoxin.o
#CFLAGS += $(DEBUG) -fPIC -I../api -I../common -Wno-pointer-to-int-cast 
#CC ?= gcc
PUNC = ../puncfilter/puncfilter.o
CFLAGS += $(DEBUG) -fPIC -I../api -I../common -I../puncfilter -Wno-int-to-pointer-cast
CC = g++
LIBDIR ?= ../../build/rfs/usr/lib/x86_64-linux-gnu
INSTALL_DIR ?= .
STRIP ?= strip --strip-unneeded

all: $(BIN)
	$(CC) -shared -Wl,-soname,libvoxin.so.1 -o $(SONAME).$(MIN).$(REV) -Wl,--version-script=libvoxin.ld $(^) $(LDFLAGS) -L$(LIBDIR) -lcommon $(PUNC)
	$(STRIP) $(SONAME).$(MIN).$(REV)

clean:
	rm -f *o *~ libvoxin.so* $(INSTALL_DIR)/libvoxin.so*

install:
	install -m 644 $(SONAME).$(MIN).$(REV) $(INSTALL_DIR)
	cd $(INSTALL_DIR); ln -s $(SONAME).$(MIN).$(REV) libvoxin.so; ln -s $(SONAME).$(MIN).$(REV) $(SONAME)
