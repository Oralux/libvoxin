# version
LIBVOXIN_VERSION_MAJOR ?= 0
LIBVOXIN_VERSION_MINOR ?= 0
LIBVOXIN_VERSION_PATCH ?= 1

SONAME=libvoxin.so.$(LIBVOXIN_VERSION_MAJOR)
MIN=$(LIBVOXIN_VERSION_MINOR)
REV=$(LIBVOXIN_VERSION_PATCH)

BIN := api.o libvoxin.o
CC ?= gcc
CFLAGS += $(DEBUG) -fPIC -I../api -I../common -Wno-int-to-pointer-cast -Wall
#CC = gcc
STRIP ?= strip --strip-unneeded

all: $(BIN)
	$(CC) -shared -Wl,-soname,libvoxin.so.$(LIBVOXIN_VERSION_MAJOR) -o $(SONAME).$(MIN).$(REV) -Wl,--version-script=libvoxin.ld $(^) $(LDFLAGS) -L$(DESTDIR)/lib -lcommon -linote 
	$(STRIP) $(SONAME).$(MIN).$(REV)

clean:
	rm -f *o *~ libvoxin.so* $(DESTDIR)/lib/libvoxin.so*

install:
	[ ! -d $(DESTDIR)/lib ] && mkdir -p $(DESTDIR)/lib || true
	install -m 644 $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib
	cd $(DESTDIR); ln -s $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib/libvoxin.so; ln -s $(SONAME).$(MIN).$(REV) $(DESTDIR)/lib/$(SONAME)
