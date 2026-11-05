# lumina-gallery : GTK3 + GStreamer (lecture video/audio), module commun.
PKGS    = gtk+-3.0 gstreamer-1.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-gallery
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-gallery.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-gallery.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
