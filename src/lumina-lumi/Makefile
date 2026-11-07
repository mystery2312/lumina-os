# lumina-lumi : Lumi, l'assistant de LuminaOS. GTK3 + module commun d'IA.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-lumi
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c ../common/lumina-ai.c

$(OUT): lumina-lumi.c lumi-extra.c lumi.h $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h ../common/lumina-ai.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-lumi.c lumi-extra.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
