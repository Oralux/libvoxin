TARGET = test1 test2 test3 test4 test5 test6 test7 test8
DOC = *.dct
CFLAGS += -I../api
LIBDIR ?= ../../build/rfs/usr/lib/x86_64-linux-gnu
INSTALL_DIR ?= ../../build/rfs/usr/bin
LDFLAGS += -L $(LIBDIR)
LDLIBS = -lvoxin
#CC=g++
CC = gcc-4.8

all: $(TARGET)

clean:
	rm -f *o *~ $(TARGET)

install:
	install -m 755 $(TARGET) $(INSTALL_DIR)
	install -m 644 $(DOC) $(INSTALL_DIR)
