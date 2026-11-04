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
VERSION=$($SUDO cat "$SRC/etc/lumina/version")
# panic=10 : un systeme qui ne demarre pas redemarre, ce qui decompte les
# essais d'une nouvelle version (retour automatique a l'ancienne).
# Options communes : /usr/share/lumina/cmdline (splash = logo au demarrage).
CMDLINE="root=PARTUUID=$UUID_ROOT_A $($SUDO cat "$SRC/usr/share/lumina/cmdline") $KERNEL_CMDLINE_EXTRA"

# On recopie noyau et initrd en local : mcopy doit pouvoir les lire.
$SUDO cp "$SRC/boot/vmlinuz-$KVER" "$W/vmlinuz"
$SUDO cp "$SRC/boot/initrd.img-$KVER" "$W/initrd.img"
$SUDO chown "$(id -u):$(id -g)" "$W/vmlinuz" "$W/initrd.img"

mkfs.vfat -F 32 -n LUMINA-ESP -C "$W/esp.img" $((ESP_MB * 1024)) >/dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i "$W/esp.img" ::/EFI ::/EFI/BOOT ::/loader ::/loader/entries ::/lumina-a
# Chemin de secours EFI/BOOT/BOOTX64.EFI : aucune variable NVRAM a ecrire,
# l'image est amorcable telle quelle sur n'importe quelle machine UEFI.
# Secure Boot : BOOTX64.EFI = shim (signe Microsoft), qui charge
# grubx64.efi a cote de lui = systemd-boot signe par Debian ; le noyau
# Debian est signe lui aussi. Sans les paquets signes, systemd-boot seul.
SHIM="$SRC/usr/lib/shim/shimx64.efi.signed"
MOK="$SRC/usr/lib/shim/mmx64.efi.signed"
SDB="$SRC/usr/lib/systemd/boot/efi/systemd-bootx64.efi.signed"
if $SUDO test -f "$SHIM" && $SUDO test -f "$SDB"; then
  $SUDO cp "$SHIM" "$W/shim.efi"; $SUDO cp "$SDB" "$W/sdboot.efi"
  $SUDO test -f "$MOK" && $SUDO cp "$MOK" "$W/mm.efi"
  $SUDO chown "$(id -u):$(id -g)" "$W"/*.efi
  mcopy -i "$W/esp.img" "$W/shim.efi"   ::/EFI/BOOT/BOOTX64.EFI
  mcopy -i "$W/esp.img" "$W/sdboot.efi" ::/EFI/BOOT/grubx64.efi
  [ -f "$W/mm.efi" ] && mcopy -i "$W/esp.img" "$W/mm.efi" ::/EFI/BOOT/mmx64.efi
  echo ">> Secure Boot : shim + systemd-boot signes"
else
  mcopy -i "$W/esp.img" /usr/lib/systemd/boot/efi/systemd-bootx64.efi ::/EFI/BOOT/BOOTX64.EFI
  echo ">> sans Secure Boot (paquets signes absents)"
fi
# --- PC sans UEFI : GRUB (BIOS) sur la meme ESP ---------------------
# boot.img dans le MBR -> core.img dans la petite partition "BIOS boot"
# (secteurs 34-2047, avant l'ESP) -> /grub/grub.cfg sur l'ESP, qui suit la
# meme logique A/B que systemd-boot (voir usr/share/lumina/grub.cfg).
G="$SRC/usr/lib/grub/i386-pc"
BIOS_GRUB=0
if $SUDO test -f "$G/boot.img"; then
  BIOS_GRUB=1
  # "(,gpt1)" : la premiere partition du disque qui a demarre
  $SUDO chroot "$SRC" grub-mkimage -O i386-pc -o /tmp/core.img -p '(,gpt1)/grub' \
    biosdisk part_gpt fat
  $SUDO mv "$SRC/tmp/core.img" "$W/core.img"
  $SUDO cp "$G/boot.img" "$W/boot.img"
  $SUDO cp -r "$G" "$W/i386-pc"
  $SUDO chown -R "$(id -u):$(id -g)" "$W/core.img" "$W/boot.img" "$W/i386-pc"
  rm -f "$W"/i386-pc/*.img "$W"/i386-pc/*.image "$W"/i386-pc/*.module 2>/dev/null || true
  CORE_SECTORS=$(( ($(stat -c %s "$W/core.img") + 511) / 512 ))
  [ "$CORE_SECTORS" -le 2013 ] || { echo "core.img trop gros ($CORE_SECTORS secteurs)"; exit 1; }
  mmd -i "$W/esp.img" ::/grub
  mcopy -i "$W/esp.img" -s "$W/i386-pc" ::/grub/
  $SUDO cp "$SRC/usr/share/lumina/grub.cfg" "$W/grub.cfg"; $SUDO chown "$(id -u):$(id -g)" "$W/grub.cfg"
  mcopy -i "$W/esp.img" "$W/grub.cfg" ::/grub/grub.cfg
  # grubenv : bloc de 1024 octets que GRUB reecrit sur place
  { printf '# GRUB Environment Block\ngood=a\nuuid_a=%s\nuuid_b=%s\nextra=%s\n' \
      "$UUID_ROOT_A" "$UUID_ROOT_B" "$KERNEL_CMDLINE_EXTRA"; } > "$W/grubenv"
  n=$(stat -c %s "$W/grubenv"); head -c $((1024 - n)) /dev/zero | tr '\0' '#' >> "$W/grubenv"
  mcopy -i "$W/esp.img" "$W/grubenv" ::/grub/grubenv
  echo ">> BIOS : GRUB ($CORE_SECTORS secteurs)"
fi

# Un noyau par emplacement (A, B) : mettre a jour B ne touche pas au noyau
# de A, vers lequel on peut toujours revenir.
mcopy -i "$W/esp.img" "$W/vmlinuz"    ::/lumina-a/vmlinuz
mcopy -i "$W/esp.img" "$W/initrd.img" ::/lumina-a/initrd.img

# Pas de "default" : systemd-boot demarre l'entree de plus haute version,
# et relegue en fin de liste une entree dont les essais sont epuises.
printf 'timeout 0\neditor no\n' > "$W/loader.conf"
mcopy -i "$W/esp.img" "$W/loader.conf" ::/loader/loader.conf
# B est vide : sa premiere entree sera ecrite par lumina-update.
printf 'title    LuminaOS %s\nsort-key luminaos\nversion  %s\nlinux    /lumina-a/vmlinuz\ninitrd   /lumina-a/initrd.img\noptions  %s\n' \
  "$VERSION" "$VERSION" "$CMDLINE" > "$W/entry-a.conf"
mcopy -i "$W/esp.img" "$W/entry-a.conf" ::/loader/entries/lumina-a.conf

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
sgdisk -a 1 \
  -n "5:34:2047"                        -t 5:ef02 -c 5:BIOS \
  -n "1:${ESP_START}M:+${ESP_MB}M"      -t 1:ef00 -c 1:ESP    -u "1:$UUID_ESP" \
  -n "2:${ROOTA_START}M:+${ROOT_MB}M"   -t 2:8304 -c 2:ROOT-A -u "2:$UUID_ROOT_A" \
  -n "3:${ROOTB_START}M:+${ROOT_MB}M"   -t 3:8304 -c 3:ROOT-B -u "3:$UUID_ROOT_B" \
  -n "4:${STATE_START}M:+${STATE_MB}M"  -t 4:8300 -c 4:STATE  -u "4:$UUID_STATE" \
  "$IMAGE" >/dev/null

if [ "$BIOS_GRUB" = 1 ]; then
  # boot.img (440 premiers octets : la table de partitions reste) et ou
  # trouver core.img : LBA 34 ; puis la liste de blocs de core.img (suite
  # de core.img : LBA 35, CORE_SECTORS-1 secteurs). Ce que fait
  # grub-bios-setup, ici sur un simple fichier.
  le() { python3 -c "import struct,sys; sys.stdout.buffer.write(struct.pack('<'+sys.argv[1], int(sys.argv[2])))" "$1" "$2"; }
  le Q 34 | dd of="$W/boot.img" bs=1 seek=$((0x5c)) conv=notrunc status=none
  le Q 35 | dd of="$W/core.img" bs=1 seek=$((0x1f4)) conv=notrunc status=none
  le H $((CORE_SECTORS - 1)) | dd of="$W/core.img" bs=1 seek=$((0x1fc)) conv=notrunc status=none
  dd if="$W/boot.img" of="$IMAGE" bs=440 count=1 conv=notrunc status=none
  dd if="$W/core.img" of="$IMAGE" bs=512 seek=34 conv=notrunc status=none
fi
dd if="$W/esp.img"   of="$IMAGE" bs=1M seek=$ESP_START   conv=notrunc status=none
dd if="$W/root.img"  of="$IMAGE" bs=1M seek=$ROOTA_START conv=notrunc status=none
dd if="$W/state.img" of="$IMAGE" bs=1M seek=$STATE_START conv=notrunc status=none
$SUDO rm -rf "$W"

echo "$VERSION" > out/image-version
echo ">> OK : $IMAGE"
echo "   taille apparente $(du -h --apparent-size "$IMAGE" | cut -f1), reelle $(du -h "$IMAGE" | cut -f1)"
sgdisk -p "$IMAGE" | tail -6
