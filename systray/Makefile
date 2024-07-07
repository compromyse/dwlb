PROGNAME    = dwlbtray
CC          = cc
RM          = rm -f
PREFIX      = /usr/local
CFLAGS      = -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -g
DEPINCLUDES = `pkg-config --cflags glib-2.0 gtk4 gtk4-layer-shell-0`
DEPLIBS     = `pkg-config --libs glib-2.0 gtk4 gtk4-layer-shell-0`
OBJS        = dwlbtray.o snwatcher.o snhost.o snitem.o sndbusmenu.o

all: $(PROGNAME)

clean:
	$(RM) $(PROGNAME) $(OBJS)

install: all
	install -D -t $(PREFIX)/bin $(PROGNAME)

dwlbtray: dwlbtray.o snwatcher.o snhost.o snitem.o sndbusmenu.o
	$(CC) $(LDFLAGS) -o $(PROGNAME) $(OBJS) $(DEPLIBS)

.SUFFIXES:
.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) $(DEPINCLUDES) -c $<

dwlbtray.o: dwlbtray.c snhost.h
snwatcher.o: snwatcher.c snwatcher.h
snhost.o: snhost.c snhost.h snwatcher.h snitem.h
snitem.o: snitem.c snitem.h sndbusmenu.h
sndbusmenu.o: sndbusmenu.c sndbusmenu.h snitem.h
