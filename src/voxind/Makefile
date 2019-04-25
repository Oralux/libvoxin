BIN = main.o
CFLAGS += -m32 -ggdb -DDEBUG -I../common -I../api
LDFLAGS += -m32 -Wl,--dynamic-linker,./lib/ld-linux.so.2
CC ?= gcc
STRIP ?= strip --strip-unneeded

voxind: $(BIN)
#	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(DESTDIR)/lib -lcommon -L$(DESTDIR)/../$(IBMTTSDIR) -libmeci -letidev $(DESTDIR)/lib/libstdc++-libc6.2-2.so.3
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(DESTDIR)/lib -lcommon -linote -L$(IBMTTSDIR)/lib -libmeci
	$(STRIP) $(@)

all: voxind

clean:
	rm -f *o *~ voxind

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 voxind $(DESTDIR)/bin/
