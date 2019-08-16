# version
VERSION ?= 0.0.1
BIN=voxin-say.o tts.o file.o wavfile.o textfile.o 
LIBS=-L$(DESTDIR)/lib -lvoxin -lcommon -ldl
CFLAGS += -g -DVERSION='"$(VERSION)"' -I../api -I../common
#STRIP ?= strip --strip-unneeded
STRIP ?=

all: voxin-say

voxin-say: $(BIN)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)
	$(STRIP) voxin-say

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 voxin-say $(DESTDIR)/bin

clean:
	rm -f voxin-say.o voxin-say

test:
	./test.sh
