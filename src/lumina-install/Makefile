# lumina-install : GTK3 et le module commun de LuminaOS.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-install
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-install.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-install.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
