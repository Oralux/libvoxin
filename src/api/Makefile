
install all:
	[ ! -d $(DESTDIR)/include ] && mkdir -p $(DESTDIR)/include || true
	install -m 444 *h $(DESTDIR)/include

clean:
	rm -f *~ $(DESTDIR)/include/*h

