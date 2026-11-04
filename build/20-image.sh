#!/bin/bash
# Assemble l'image disque amorcable.
#   mke2fs -d  fabrique l'ext4 directement depuis le repertoire : aucun
#              montage, aucun peripherique loop a manipuler.
#   mcopy      remplit l'ESP (FAT32) sans le monter.
#   sgdisk     ecrit la table de partitions GPT dans un simple fichier.
#   dd         place chaque systeme de fichiers a son offset.
set -euo pipefail
cd "$(dirname "$0")/.."
source config/image.conf

SUDO=""; [ "$BUILD_MODE" = "root" ] && SUDO="sudo"
W=out/work
$SUDO rm -rf "$W"; mkdir -p "$W"

# --- source du rootfs -------------------------------------------------
if [ "$BUILD_MODE" = "root" ]; then
  SRC="$ROOTFS_DIR"
  [ -d "$SRC" ] || { echo "rootfs absent : lance build/10-rootfs.sh"; exit 1; }
else
  # Mode sans privileges : on deballe l'archive dans un namespace ou
  # l'uid 0 correspond a notre utilisateur, pour que mke2fs enregistre
  # les bons proprietaires.
  [ -f "$ROOTFS_TAR" ] || { echo "archive absente : lance build/10-rootfs.sh"; exit 1; }
  SRC="$W/rootfs"; mkdir -p "$SRC"
  unshare --user --map-auto --map-root-user \
    tar -xpf "$ROOTFS_TAR" --numeric-owner -C "$SRC"
fi

# --- offsets (MiB), alignes sur 1 MiB ---------------------------------
ESP_START=1
ROOTA_START=$((ESP_START + ESP_MB))
ROOTB_START=$((ROOTA_START + ROOT_MB))
STATE_START=$((ROOTB_START + ROOT_MB))
STATE_MB=$((TOTAL_MB - STATE_START - 1))

# --- 1. ESP : noyau + initrd + systemd-boot ---------------------------
KVER=$($SUDO sh -c "ls -1 '$SRC'/boot/vmlinuz-* | sort -V | tail -1" | sed 's|.*/vmlinuz-||')
[ -n "$KVER" ] || { echo "aucun noyau trouve dans $SRC/boot"; exit 1; }
echo ">> noyau $KVER"
CMDLINE="root=PARTUUID=$UUID_ROOT_A rw rootwait quiet loglevel=3 $KERNEL_CMDLINE_EXTRA"

# On recopie noyau et initrd en local : mcopy doit pouvoir les lire.
$SUDO cp "$SRC/boot/vmlinuz-$KVER" "$W/vmlinuz"
$SUDO cp "$SRC/boot/initrd.img-$KVER" "$W/initrd.img"
$SUDO chown "$(id -u):$(id -g)" "$W/vmlinuz" "$W/initrd.img"

mkfs.vfat -F 32 -n LUMINA-ESP -C "$W/esp.img" $((ESP_MB * 1024)) >/dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i "$W/esp.img" ::/EFI ::/EFI/BOOT ::/loader ::/loader/entries
# systemd-boot pose en BOOTX64.EFI : aucune variable NVRAM a ecrire,
# l'image est amorcable telle quelle sur n'importe quelle machine UEFI.
mcopy -i "$W/esp.img" /usr/lib/systemd/boot/efi/systemd-bootx64.efi ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$W/esp.img" "$W/vmlinuz"    ::/vmlinuz
mcopy -i "$W/esp.img" "$W/initrd.img" ::/initrd.img

printf 'default lumina-a.conf\ntimeout 0\neditor no\n' > "$W/loader.conf"
mcopy -i "$W/esp.img" "$W/loader.conf" ::/loader/loader.conf
printf 'title LuminaOS (A)\nlinux /vmlinuz\ninitrd /initrd.img\noptions %s\n' \
  "$CMDLINE" > "$W/entry-a.conf"
mcopy -i "$W/esp.img" "$W/entry-a.conf" ::/loader/entries/lumina-a.conf
printf 'title LuminaOS (B)\nlinux /vmlinuz\ninitrd /initrd.img\noptions %s\n' \
  "root=PARTUUID=$UUID_ROOT_B rw rootwait quiet loglevel=3 $KERNEL_CMDLINE_EXTRA" \
  > "$W/entry-b.conf"
mcopy -i "$W/esp.img" "$W/entry-b.conf" ::/loader/entries/lumina-b.conf

# --- 2. ROOT-A : ext4 fabrique depuis le repertoire -------------------
echo ">> mke2fs root (${ROOT_MB} MiB)"
$SUDO mke2fs -q -t ext4 -L LUMINA-ROOT -d "$SRC" -E root_owner=0:0 \
      "$W/root.img" "${ROOT_MB}M"
$SUDO chown "$(id -u):$(id -g)" "$W/root.img"

# --- 3. STATE : partition d'etat, vide --------------------------------
mke2fs -q -t ext4 -L LUMINA-STATE -E lazy_itable_init=0,lazy_journal_init=0 "$W/state.img" "${STATE_MB}M"

# --- 4. GPT + ecriture des systemes de fichiers -----------------------
echo ">> assemblage $IMAGE"
rm -f "$IMAGE"; truncate -s "${TOTAL_MB}M" "$IMAGE"
sgdisk -Z "$IMAGE" >/dev/null 2>&1 || true
sgdisk \
  -n "1:${ESP_START}M:+${ESP_MB}M"      -t 1:ef00 -c 1:ESP    -u "1:$UUID_ESP" \
  -n "2:${ROOTA_START}M:+${ROOT_MB}M"   -t 2:8304 -c 2:ROOT-A -u "2:$UUID_ROOT_A" \
  -n "3:${ROOTB_START}M:+${ROOT_MB}M"   -t 3:8304 -c 3:ROOT-B -u "3:$UUID_ROOT_B" \
  -n "4:${STATE_START}M:+${STATE_MB}M"  -t 4:8300 -c 4:STATE  -u "4:$UUID_STATE" \
  "$IMAGE" >/dev/null

dd if="$W/esp.img"   of="$IMAGE" bs=1M seek=$ESP_START   conv=notrunc status=none
dd if="$W/root.img"  of="$IMAGE" bs=1M seek=$ROOTA_START conv=notrunc status=none
dd if="$W/state.img" of="$IMAGE" bs=1M seek=$STATE_START conv=notrunc status=none
$SUDO rm -rf "$W"

echo ">> OK : $IMAGE"
echo "   taille apparente $(du -h --apparent-size "$IMAGE" | cut -f1), reelle $(du -h "$IMAGE" | cut -f1)"
sgdisk -p "$IMAGE" | tail -6
