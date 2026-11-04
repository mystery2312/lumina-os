#!/bin/bash
# Compile les programmes LuminaOS (bureau, fichiers, installateur) sur l'hote. Ils ne
# dependent que de GTK3, gtk-layer-shell et GIO, dont l'ABI est stable :
# compiles contre la glibc 2.39 d'Ubuntu 24.04, ils tournent sur la 2.41
# de Debian 13.
set -euo pipefail
cd "$(dirname "$0")/.."
for app in lumina-shell lumina-files lumina-install; do
  make -s -C "src/$app" OUT="$PWD/out/bin/$app"
  echo ">> $app : $(du -h "out/bin/$app" | cut -f1)"
done
