##LIBS=-L/opt/IBM/ibmtts/lib -libmeci -lchtrom
#LIBS=-L/opt/IBM/ibmtts/lib -libmeci -lcommon
#CFLAGS += -I/opt/IBM/ibmtts/inc/ -I../common  -m32
LIBS=-L../../build/rfs/usr/lib/x86_64-linux-gnu -lvoxin -lcommon
CFLAGS += -I../api -I../common
STRIP ?= strip --strip-unneeded
CC = gcc-4.8

all: say

say: say.o
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)
	$(STRIP) say

clean:
	rm -f say.o say
