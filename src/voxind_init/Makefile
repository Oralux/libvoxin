TARGET = voxind_init
CFLAGS += -I../common
LDFLAGS += -L $(DESTDIR)/lib
LDLIBS = -lcommon

all: voxind_init

clean:
	rm -f *o *~ $(TARGET)

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 voxind_init $(DESTDIR)/bin
	chmod u+s $(DESTDIR)/bin/voxind_init

