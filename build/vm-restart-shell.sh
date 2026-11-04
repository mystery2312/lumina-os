#!/bin/bash
# Relance lumina-shell dans la session graphique de la VM, sans redemarrer.
cd "$(dirname "$0")/.."
exec build/vm-run.sh "pkill -x lumina-shell; sleep 1; runuser -u lumina -- env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland setsid -f lumina-shell >/dev/null 2>&1; sleep 2; pgrep -x lumina-shell >/dev/null && echo shell relance" 60
