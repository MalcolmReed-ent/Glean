# Glean - Reddit Scraper
# See LICENSE file for copyright and license details.

include config.mk

SRC = glean.c
OBJ = ${SRC:.c=.o}

all: options glean

options:
	@echo glean build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

glean: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f glean ${OBJ} glean-${VERSION}.tar.gz

dist: clean
	mkdir -p glean-${VERSION}
	cp -R LICENSE Makefile README.md config.mk ${SRC} glean.1 glean-${VERSION}
	tar -cf glean-${VERSION}.tar glean-${VERSION}
	gzip glean-${VERSION}.tar
	rm -rf glean-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f glean ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/glean
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < glean.1 > ${DESTDIR}${MANPREFIX}/man1/glean.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/glean.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/glean
	rm -f ${DESTDIR}${MANPREFIX}/man1/glean.1

.PHONY: all options clean dist install uninstall
