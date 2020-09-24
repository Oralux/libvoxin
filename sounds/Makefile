# version

SOUNDS_DIR = $(DESTDIR)/share/sounds

all:
	echo none

install:
	install --target-directory=$(SOUNDS_DIR) -D -m 644 LICENSE
	install --target-directory=$(DESTDIR)/share/sounds/11025 -D -m 644 11025/*wav 
	install --target-directory=$(DESTDIR)/share/sounds/22050 -D -m 644 22050/*wav 

clean:
	rm -f *~ $(DESTDIR)/share/sounds
