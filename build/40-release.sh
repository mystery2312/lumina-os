#!/bin/bash
# Prepare une mise a jour a partir du rootfs construit (out/rootfs) :
#   out/release/<version>/root-<version>.img.zst   image systeme compressee
#   out/release/<version>/manifest.txt             version, empreinte, taille
#
#   build/40-release.sh            prepare les fichiers (test local)
#   build/40-release.sh --publish  les publie en Release GitHub ("latest")
#
# Les machines LuminaOS lisent le manifeste de la derniere Release
# (UPDATE_URL dans config/image.conf) puis telechargent l'image.
set -euo pipefail
cd "$(dirname "$0")/.."
source config/image.conf
[ -d "$ROOTFS_DIR" ] || { echo "rootfs absent : lance build/10-rootfs.sh"; exit 1; }

VERSION=$(sudo cat "$ROOTFS_DIR/etc/lumina/version")
BUILD_DEV=$(sudo sed -n 's/^DEV=//p' "$ROOTFS_DIR/etc/lumina/build.conf")
D=out/release/$VERSION
mkdir -p "$D"
RAW="$D/root-$VERSION.img"
ZST="$RAW.zst"

# ext4 a la taille du contenu + 35 % de marge ; lumina-update l'agrandit
# ensuite a toute la partition. Doit tenir dans la plus petite partition
# systeme (3 Go dans l'image, 4 Go apres installation).
USED_MB=$(( $(sudo du -sxm "$ROOTFS_DIR" | cut -f1) ))
FS_MB=$(( USED_MB * 135 / 100 + 128 ))
[ "$FS_MB" -le "$ROOT_MB" ] || { echo "systeme trop gros : $FS_MB Mo > $ROOT_MB Mo"; exit 1; }
echo ">> image systeme $VERSION : $FS_MB Mo (contenu $USED_MB Mo)"
rm -f "$RAW" "$ZST"
sudo mke2fs -q -t ext4 -L LUMINA-ROOT -d "$ROOTFS_DIR" -E root_owner=0:0 "$RAW" "${FS_MB}M"
sudo chown "$(id -u):$(id -g)" "$RAW"
BYTES=$(stat -c %s "$RAW")
echo ">> compression"
zstd -q -T0 -10 --rm "$RAW" -o "$ZST"
SUM=$(sha256sum "$ZST" | cut -d' ' -f1)

cat > "$D/manifest.txt" <<EOF
VERSION=$VERSION
ROOT_FILE=$(basename "$ZST")
ROOT_SHA256=$SUM
ROOT_BYTES=$BYTES
EOF
echo ">> $D : $(du -h "$ZST" | cut -f1) compresse"
cat "$D/manifest.txt"

if [ "${1:-}" = "--publish" ]; then
  # Une image de developpement a un mot de passe root connu : jamais publiee.
  [ "$BUILD_DEV" = 0 ] || { echo "REFUS : image de developpement (DEV=$BUILD_DEV). Reconstruire avec DEV=0."; exit 1; }
  gh release create "v$VERSION" "$D/manifest.txt" "$ZST" \
    --title "LuminaOS $VERSION" --latest \
    --notes "Mise à jour système LuminaOS $VERSION, installée automatiquement sur les machines LuminaOS."
fi
