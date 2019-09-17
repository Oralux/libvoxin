TARGET = test1 test2 test3 test4 test5 test6 test7 test8 test9 testve5 testve6 test14
DOC = *.dct
CFLAGS += -I../api -DPATHNAME_RAW_DATA=\"/tmp/test_libvoxin.raw\"
LDFLAGS += -L $(DESTDIR)/lib
LDLIBS = -lvoxin -ldl
#CC=g++

all: $(TARGET)

clean:
	rm -f *o *~ $(TARGET)

install:
	[ ! -d $(DESTDIR)/bin ] && mkdir -p $(DESTDIR)/bin || true
	install -m 755 $(TARGET) $(DESTDIR)/bin/
	install -m 644 $(DOC) $(DESTDIR)/bin/
