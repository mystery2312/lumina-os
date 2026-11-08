#!/bin/bash
# Seule etape qui demande sudo : installer les outils de construction.
# Tout le reste de la chaine tourne sans privileges.
set -euo pipefail
PKGS=(
  mmdebstrap          # debootstrap moderne, fonctionne sans root (--mode=unshare)
  uidmap              # namespaces utilisateur, requis par --mode=unshare
  e2fsprogs           # mke2fs -d : cree un ext4 depuis un repertoire, sans mount
  mtools              # mcopy : ecrit dans un FAT sans mount
  dosfstools gdisk
  systemd-boot-efi    # le binaire EFI qu'on copie sur l'ESP
  qemu-system-x86 qemu-utils ovmf
  zstd xz-utils
  libsqlite3-dev      # lumina-index
  cmake g++           # whisper.cpp (voix de Lumi)
)
echo "Installation : ${PKGS[*]}"
sudo apt-get update
sudo apt-get install -y --no-install-recommends "${PKGS[@]}"
echo "OK. Les etapes suivantes ne demandent plus sudo."
