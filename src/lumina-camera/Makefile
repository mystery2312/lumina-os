# lumina-camera : GTK3, module commun de LuminaOS.
PKGS    = gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0
CFLAGS ?= -O2 -g0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
OUT    ?= lumina-camera
COMMON  = ../common/lumina-ui.c ../common/lumina-sys.c

$(OUT): lumina-camera.c $(COMMON) ../common/lumina-ui.h ../common/lumina-sys.h
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -I../common $$(pkg-config --cflags $(PKGS)) -o $@ lumina-camera.c $(COMMON) \
	      $$(pkg-config --libs $(PKGS)) -lm
	strip $@
