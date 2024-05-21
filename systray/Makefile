BINS = dwlbtray
OBJS = dwlbtray.o snhost.o snitem.o sndbusmenu.o
DEPS = snhost.h snitem.h sndbusmenu.h

PREFIX ?= /usr/local
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -g

all: $(BINS)

clean:
	$(RM) $(BINS) $(OBJS)

install: all
	install -D -t $(PREFIX)/bin $(BINS)

%.o: $(DEPS)
dwlbtray: $(OBJS)

dwlbtray: CFLAGS+=$(shell pkg-config --cflags glib-2.0 gtk4 gtk4-layer-shell-0)
dwlbtray: LDLIBS+=$(shell pkg-config --libs glib-2.0 gtk4 gtk4-layer-shell-0)

.PHONY: all clean install
