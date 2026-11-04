#!/bin/bash
# Envoie des combinaisons de touches a la VM (lancee avec HEADLESS=1).
# Usage : build/vm-key.sh meta_l-left alt-tab f11 ...   (noms QEMU)
cd "$(dirname "$0")/.."
for k in "$@"; do
  printf 'sendkey %s 200\n' "$k" | socat - UNIX-CONNECT:out/qemu-monitor.sock >/dev/null
  sleep 2
done
