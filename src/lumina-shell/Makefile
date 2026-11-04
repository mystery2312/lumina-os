# lumina-shell : GTK3 + gtk-layer-shell (+ epoxy pour la sonde GL).
PKGS    = gtk+-3.0 gtk-layer-shell-0 epoxy
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-shell

$(OUT): lumina-shell.c
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $$(pkg-config --cflags $(PKGS)) -o $@ $< $$(pkg-config --libs $(PKGS)) -lm
	strip $@
