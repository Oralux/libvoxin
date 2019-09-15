BIN := msg.o pipe.o debug.o

#CFLAGS += -ggdb -DDEBUG -fPIC -I. -I../api
CFLAGS += -fPIC -I. -I../api
CC ?= gcc
AR ?= ar

all: $(BIN)
	$(AR) rcs libcommon.a $(^)

install:
	[ ! -d $(DESTDIR)/lib ] && mkdir -p $(DESTDIR)/lib || true
	mv libcommon.a $(DESTDIR)/lib/

clean:
	rm -f *o *~ *a
