BIN1 = test1.o
BIN2 = test2.o
BIN3 = test3.o
BIN4 = test4.o
BIN5 = test5.o
BIN6 = test6.o
TARGET = client1 client2 client3 client4 client5 client6
DOC = main1.dct
CFLAGS += -I../api
STRIP ?= strip --strip-unneeded
CC ?= gcc
LIBDIR ?= ../../build/rfs/usr/lib/x86_64-linux-gnu
INSTALL_DIR ?= ../../build/rfs/usr/bin

client1: $(BIN1)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

client2: $(BIN2)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

client3: $(BIN3)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

client4: $(BIN4)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

client5: $(BIN5)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

client6: $(BIN6)
	$(CC) -o $(@) $(^) $(LDFLAGS) -L$(LIBDIR) -lvoxin
	$(STRIP) $(@)

all: $(TARGET)

clean:
	rm -f *o *~ $(TARGET)

install:
	install -m 755 $(TARGET) $(INSTALL_DIR)
	install -m 644 $(DOC) $(INSTALL_DIR)
