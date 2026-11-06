# lumina-tasks : gestionnaire de taches, GTK3.
PKGS    = gtk+-3.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-tasks
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-tasks.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-tasks.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
