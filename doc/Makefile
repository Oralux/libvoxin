OBJS := $(patsubst %.puml,%.png,$(wildcard *.puml))

%.png : %.puml
	plantuml $<

all : $(OBJS)

clean:
	rm -f *~ *png

.PHONY:	clean all
