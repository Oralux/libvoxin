# version
VERMAJ ?= 0
VERMIN ?= 0
VERMIC ?= 1

SONAME=libvoxin.so.$(VERMAJ)
MIN=$(VERMIN)
REV=$(VERMIC)

BIN := api.o libvoxin.o
CC ?= gcc
CFLAGS += $(DEBUG) -fPIC -I../api -I../common -Wno-int-to-pointer-cast -Wall
#CC = gcc
STRIP ?= strip --strip-unneeded

all: $(BIN)
	$(CC) -shared -Wl,-soname,libvoxin.so.$(VERMAJ) -o $(SONAME).$(MIN).$(REV) -Wl,--version-script=libvoxin.ld $(^) $(LDFLAGS) -L$(DESTDIR)/lib -lcommon -linote 
	$(STRIP) $(SONAME).$(MIN).$(REV)

clean:
	rm -f *o *~ libvoxin.so* $(DESTDIR)/lib/libvoxin.so*

install:
	[ ! -d $(DESTDIR)/lib ] && mkdir -p $(DESTDIR)/lib || true
	install -m 644 $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib
	cd $(DESTDIR); ln -s $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib/libvoxin.so; ln -s $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib/$(SONAME)
