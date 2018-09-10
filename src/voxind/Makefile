BIN = main.o
CFLAGS += -m32 -ggdb -DDEBUG -I../common -I../api
LDFLAGS += -m32
CC ?= gcc
STRIP ?= strip --strip-unneeded
IBMTTSDIR = /opt/IBM/ibmtts/lib/

voxind: $(BIN)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(DESTDIR)/lib -lcommon -L$(DESTDIR)/../$(IBMTTSDIR) -Wl,-rpath=$(IBMTTSDIR) -libmeci -letidev
	$(STRIP) $(@)

all: voxind

clean:
	rm -f *o *~ voxind

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 voxind $(DESTDIR)/bin/
