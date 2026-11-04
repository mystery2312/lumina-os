#!/bin/bash
# Compile lumina-shell sur l'hote. Le binaire ne depend que de GTK3 et
# gtk-layer-shell, dont l'ABI est stable : compile contre la glibc 2.39
# d'Ubuntu 24.04, il tourne sur la 2.41 de Debian 13.
set -euo pipefail
cd "$(dirname "$0")/.."
make -s -C src/lumina-shell OUT="$PWD/out/bin/lumina-shell"
echo ">> lumina-shell : $(du -h out/bin/lumina-shell | cut -f1)"
