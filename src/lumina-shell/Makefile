# lumina-shell : GTK3 + gtk-layer-shell (+ le protocole wlr-foreign-toplevel
# pour suivre les fenetres ouvertes).
PKGS    = gtk+-3.0 gtk-layer-shell-0 wayland-client
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-shell
GEN    ?= $(dir $(OUT))gen
PROTO   = protocols/wlr-foreign-toplevel-management-unstable-v1.xml

COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-shell.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h $(GEN)/foreign-toplevel.h $(GEN)/foreign-toplevel.c
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I$(GEN) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-shell.c $(COMMON) $(GEN)/foreign-toplevel.c \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@

$(GEN)/foreign-toplevel.h: $(PROTO)
	mkdir -p $(GEN)
	wayland-scanner client-header $< $@

$(GEN)/foreign-toplevel.c: $(PROTO)
	mkdir -p $(GEN)
	wayland-scanner private-code $< $@
