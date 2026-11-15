PREFIX = /usr/local
CFLAGS = -O2 -Wall -Wextra
LIBS   = -lX11 -lXfixes

clipd: clipd.c config.h
	$(CC) $(CFLAGS) -o $@ clipd.c $(LIBS)

install: clipd
	install -Dm755 clipd $(DESTDIR)$(PREFIX)/bin/clipd

clean:
	rm -f clipd

.PHONY: install clean
