# lumina-welcome : GTK3 et les modules communs de LuminaOS.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-welcome
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-welcome.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-welcome.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
