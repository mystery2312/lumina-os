#!/bin/bash
set -euo pipefail
D="$(dirname "$0")"
"$D/05-shell.sh"
"$D/06-ai.sh"
"$D/10-rootfs.sh"
"$D/20-image.sh"
echo
echo "Pret. Pour demarrer :  build/30-qemu.sh"
