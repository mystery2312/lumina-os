# lumina-greeter : ecran de connexion (greetd), GTK3.
PKGS    = gtk+-3.0 gio-unix-2.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-greeter
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-greeter.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-greeter.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
