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

# ext4 a la taille du contenu + 20 % (inodes, journal, blocs partiels) ;
# lumina-update l'agrandit ensuite a toute la partition. Doit tenir dans la
# partition systeme (ROOT_MB = 3072 Mo, fixee a l'installation : une mise a
# jour ne peut jamais la depasser).
USED_MB=$(( $(sudo du -sxm "$ROOTFS_DIR" | cut -f1) ))
FS_MB=$(( USED_MB * 120 / 100 + 128 ))
[ "$FS_MB" -le "$ROOT_MB" ] || { echo "systeme trop gros : $FS_MB Mo > $ROOT_MB Mo"; exit 1; }
echo ">> image systeme $VERSION : $FS_MB Mo (contenu $USED_MB Mo)"
rm -f "$RAW" "$ZST"
sudo mke2fs -q -t ext4 -m 0 -L LUMINA-ROOT -d "$ROOTFS_DIR" -E root_owner=0:0 "$RAW" "${FS_MB}M"
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

# Signature : le manifeste (version + empreinte de l'image) est signe avec
# la cle privee, gardee HORS du depot. Les machines le verifient avec la
# cle publique /etc/lumina/update.pub avant d'ecrire quoi que ce soit.
KEY=${LUMINA_SIGN_KEY:-$HOME/.config/lumina-os/update.key}
[ -f "$KEY" ] || { echo "cle de signature absente : $KEY"; exit 1; }
rm -f "$D/manifest.txt.minisig"
minisign -S -s "$KEY" -m "$D/manifest.txt" -t "LuminaOS $VERSION" </dev/null
minisign -V -q -p overlay/etc/lumina/update.pub -m "$D/manifest.txt"
echo ">> manifeste signe"

# Image disque complete (cle USB), si elle vient du meme rootfs : xz, lu
# par balenaEtcher, Rufus, GNOME Disques et Raspberry Pi Imager.
ASSETS=("$D/manifest.txt" "$D/manifest.txt.minisig" "$ZST")
DISK_XZ="$D/lumina-os-$VERSION.img.xz"
if [ -f "$IMAGE" ] && [ "$(cat out/image-version 2>/dev/null)" = "$VERSION" ]; then
  if [ ! -f "$DISK_XZ" ]; then
    echo ">> image cle USB (xz)"
    xz -T0 -6 -c "$IMAGE" > "$DISK_XZ.part" && mv "$DISK_XZ.part" "$DISK_XZ"
  fi
  ASSETS+=("$DISK_XZ")
fi
( cd "$D" && sha256sum $(for f in "${ASSETS[@]}"; do basename "$f"; done) > SHA256SUMS )
ASSETS+=("$D/SHA256SUMS")
ls -lh "${ASSETS[@]}"

if [ "${1:-}" = "--publish" ]; then
  # Une image de developpement a un mot de passe root connu : jamais publiee.
  [ "$BUILD_DEV" = 0 ] || { echo "REFUS : image de developpement (DEV=$BUILD_DEV). Reconstruire avec DEV=0."; exit 1; }
  NOTES=${RELEASE_NOTES:-"Mise à jour système LuminaOS $VERSION, installée automatiquement sur les machines LuminaOS."}
  gh release create "v$VERSION" "${ASSETS[@]}" \
    --title "${RELEASE_TITLE:-LuminaOS $VERSION}" --latest --notes "$NOTES"
fi
