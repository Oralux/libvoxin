# version
VERSION ?= 0.0.1
LIBS=-L$(DESTDIR)/lib -lvoxin -lcommon
CFLAGS += -DVERSION='"$(VERSION)"' -I../api -I../common
STRIP ?= strip --strip-unneeded

all: say

say: say.o
	gcc -o $@ $< $(CFLAGS) $(LIBS)
	$(STRIP) say

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 say $(DESTDIR)/bin

clean:
	rm -f say.o say

test:
	./test.sh
