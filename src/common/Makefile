BIN := msg.o pipe.o debug.o
#CFLAGS += -ggdb -DDEBUG -fPIC -I. -I../api
CFLAGS += -fPIC -I. -I../api
CC ?= gcc
AR ?= ar
INSTALL_DIR ?= .

all: $(BIN)
	$(AR) rcs libcommon.a $(^)

install:
	mv libcommon.a $(INSTALL_DIR)

clean:
	rm -f *o *~ *a
