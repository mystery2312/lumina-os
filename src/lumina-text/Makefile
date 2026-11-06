# lumina-text : GTK3 + GtkSourceView, et le module commun de LuminaOS.
PKGS    = gtk+-3.0 gtksourceview-4
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-text
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c ../common/lumina-ai.c

$(OUT): lumina-text.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h ../common/lumina-ai.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-text.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
