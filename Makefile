PREFIX = /usr
LD = ld
CC = cc
INSTALL = install
CFLAGS = -g -Wall -Wextra
LDFLAGS =
VLC_PLUGIN_CFLAGS := $(shell pkg-config --cflags vlc-plugin)
VLC_PLUGIN_LIBS := $(shell pkg-config --libs vlc-plugin)

libdir = $(PREFIX)/lib
plugindir = $(libdir)/vlc/plugins

override CC += -std=gnu99
override CPPFLAGS += -DPIC -I. -Isrc
override CFLAGS += -fPIC
override LDFLAGS += -Wl,-no-undefined,-z,defs -lgif

override CPPFLAGS += -DMODULE_STRING=\"anigif\"
override CFLAGS += $(VLC_PLUGIN_CFLAGS)
override LDFLAGS += $(VLC_PLUGIN_LIBS)

TARGETS = libanigif_plugin.so

all: libanigif_plugin.so

install: all
	mkdir -p -- $(DESTDIR)$(plugindir)/codec
	$(INSTALL) --mode 0755 libanigif_plugin.so $(DESTDIR)$(plugindir)/codec

install-strip:
	$(MAKE) install INSTALL="$(INSTALL) -s"

uninstall:
	rm -f $(plugindir)/codec/libanigif_plugin.so

clean:
	rm -f -- libanigif_plugin.so src/*.o

mostlyclean: clean

SOURCES = anigif.c

$(SOURCES:%.c=src/%.o): %: src/anigif.h

libanigif_plugin.so: $(SOURCES:%.c=src/%.o)
	$(CC) $(LDFLAGS) -shared -o $@ $^

.PHONY: all install install-strip uninstall clean mostlyclean
