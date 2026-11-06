# lumina-settings : GTK3 et les modules communs de LuminaOS.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-settings
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c ../common/lumina-ai.c

$(OUT): lumina-settings.c settings-more.c settings-lumi.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h ../common/lumina-ai.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-settings.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
