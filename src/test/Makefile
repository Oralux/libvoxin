OBJS := $(patsubst %.c,%.o,$(wildcard *.c))
TARGET := $(OBJS:%.o=%)
DOC = *.dct
CFLAGS += -I../api -DPATHNAME_RAW_DATA=\"/tmp/test_libvoxin.raw\"
LDFLAGS += -L $(DESTDIR)/lib
LDLIBS = -lvoxin -ldl
#CC=g++

all: $(TARGET) $(OBJS)

clean:
	rm -f *o *~ $(TARGET)

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 $(TARGET) $(DESTDIR)/bin/
	install -m 644 $(DOC) $(DESTDIR)/bin/
