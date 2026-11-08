# lumina-index : index du contenu des fichiers. GLib + SQLite (FTS5).
PKGS    = glib-2.0 sqlite3
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-index

$(OUT): lumina-index.c
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $$(pkg-config --cflags $(PKGS)) -o $@ lumina-index.c $$(pkg-config --libs $(PKGS))
	strip $@
