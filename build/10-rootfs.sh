#!/bin/bash
# Construit le rootfs Debian.
#
# BUILD_MODE=root    : sudo + sortie en repertoire (out/rootfs/).
# BUILD_MODE=unshare : sans privileges + sortie en archive (out/rootfs.tar).
#   En mode unshare, root dans le namespace correspond a une subuid et non
#   a notre utilisateur : mmdebstrap ne peut alors ni ecrire dans $HOME ni
#   produire un repertoire aux bons proprietaires (man mmdebstrap, MODES).
#   L'archive est en revanche garantie identique bit a bit a celle d'un
#   vrai root.
set -euo pipefail
cd "$(dirname "$0")/.."
source config/image.conf

PKGS=$(grep -vE '^\s*(#|$)' config/packages.list | tr '\n' ',' | sed 's/,$//')
if [ "$FIRMWARE" = "1" ]; then
  PKGS="$PKGS,$(grep -vE '^\s*(#|$)' config/firmware.list | tr '\n' ',' | sed 's/,$//')"
  echo ">> firmwares non libres inclus (FIRMWARE=0 pour les exclure)"
fi

for b in lumina-shell lumina-files lumina-install; do
  [ -x "out/bin/$b" ] || { echo "$b absent : lance build/05-shell.sh"; exit 1; }
done
# Overlay + binaires empaquetes avec root comme proprietaire : copies tels
# quels, ils garderaient l'UID de l'utilisateur qui construit (1000), qui
# est justement celui de "lumina" dans l'image.
mkdir -p out
tar --owner=0 --group=0 --numeric-owner --mode=go-w -C overlay -cf out/overlay.tar .
tar --owner=0 --group=0 --numeric-owner --mode=go-w --transform 's|^|usr/local/bin/|' \
    -C out/bin -rf out/overlay.tar lumina-shell lumina-files lumina-install
if [ "$BUILD_MODE" = "root" ]; then
  SUDO="sudo"; MM_MODE="root"; MM_FORMAT="directory"; TARGET="$ROOTFS_DIR"
  $SUDO rm -rf "$TARGET"; mkdir -p "$TARGET"
else
  SUDO=""; MM_MODE="unshare"; MM_FORMAT="tar"; TARGET="$ROOTFS_TAR"
  rm -f "$TARGET"
fi

echo ">> LuminaOS $VERSION (DEV=$DEV)"
echo "$VERSION" > out/version
echo ">> mmdebstrap $SUITE ($ARCH), mode=$MM_MODE -> $TARGET"
$SUDO mmdebstrap \
  --mode="$MM_MODE" \
  --format="$MM_FORMAT" \
  --architectures="$ARCH" \
  --components="$COMPONENTS" \
  --keyring="$KEYRING" \
  --variant=important \
  --include="$PKGS" \
  --aptopt='Apt::Install-Recommends "false"' \
  --dpkgopt='path-exclude=/usr/share/man/*' \
  --dpkgopt='path-exclude=/usr/share/doc/*' \
  --customize-hook="echo '$HOSTNAME' > \"\$1/etc/hostname\"" \
  --customize-hook='tar-in out/overlay.tar /' \
  --customize-hook="printf 'VERSION=%s\nDEV=%s\nUPDATE_URL=%s\n' '$VERSION' '$DEV' '$UPDATE_URL' > \"\$1/etc/lumina/build.conf\"" \
  --customize-hook="echo '$VERSION' > \"\$1/etc/lumina/version\"" \
  --customize-hook='chroot "$1" /usr/local/sbin/lumina-setup' \
  "$SUITE" "$TARGET" "$MIRROR"

echo ">> resultat : $($SUDO du -sh "$TARGET" | cut -f1)"
