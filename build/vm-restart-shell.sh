#!/bin/bash
# Relance lumina-shell dans la session graphique de la VM, sans redemarrer.
# L'autostart relance le bureau apres un arret anormal : on le laisse faire,
# l'instance unique evite les doublons.
cd "$(dirname "$0")/.."
exec build/vm-run.sh "pkill -x lumina-shell; sleep 1.5; pgrep -x lumina-shell >/dev/null || runuser -u lumina -- env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus setsid -f lumina-shell >/dev/null 2>&1; sleep 2; pgrep -x lumina-shell | wc -l" 60
