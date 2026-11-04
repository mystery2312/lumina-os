# lumina-files : GTK3 + GIO, et le module commun de LuminaOS.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-files
COMMON  = ../common/lumina-ui.c

$(OUT): lumina-files.c $(COMMON) ../common/lumina-ui.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-files.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
