#!/bin/bash
# Demarre l'image dans QEMU. Par defaut 2 Go de RAM : c'est la cible.
#
#   build/30-qemu.sh             fenetre graphique (necessite un ecran)
#   LIVE_USB=1 ...               l'image est branchee comme une CLE USB, avec
#                                un disque interne vierge (out/disque-pc.img,
#                                32 Go) : teste l'installateur
#   INSTALLED=1 ...              demarre seulement ce disque interne
#   HEADLESS=1 build/30-qemu.sh  sans ecran : journal serie dans out/serial.log,
#                                terminal via build/vm-run.sh, captures via
#                                build/screenshot.sh, ecran en VNC sur
#                                127.0.0.1:5900 (tunnel SSH si distant)
set -euo pipefail
cd "$(dirname "$0")/.."
source config/image.conf
RAM="${RAM:-2048}"
# SECUREBOOT=1 : firmware avec Secure Boot actif et cles Microsoft
if [ "${SECUREBOOT:-0}" = 1 ]; then
  OVMF=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd
  VARS=out/OVMF_VARS_SB.fd
  [ -f "$VARS" ] || cp /usr/share/OVMF/OVMF_VARS_4M.ms.fd "$VARS"
else
  OVMF=/usr/share/OVMF/OVMF_CODE_4M.fd
  VARS=out/OVMF_VARS.fd
  [ -f "$VARS" ] || cp /usr/share/OVMF/OVMF_VARS_4M.fd "$VARS"
fi
[ -f "$OVMF" ] || { echo "OVMF introuvable : sudo apt install ovmf"; exit 1; }
ACCEL=$([ -w /dev/kvm ] && echo kvm || echo tcg)
PC_DISK=out/disque-pc.img

# BIOS=1 : PC ancien sans UEFI (SeaBIOS)
if [ "${BIOS:-0}" = 1 ]; then FW=(); else FW=(
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF"
  -drive if=pflash,format=raw,unit=1,file="$VARS"
); fi
SB_MACHINE=""; SB_ARGS=()
if [ "${SECUREBOOT:-0}" = 1 ]; then
  SB_MACHINE=",smm=on"; SB_ARGS=(-global driver=cfi.pflash01,property=secure,value=on)
fi
ARGS=(
  -machine "q35,accel=$ACCEL$SB_MACHINE" -cpu max -smp 2 -m "$RAM"
  "${SB_ARGS[@]}"
  "${FW[@]}"
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0
  -device virtio-tablet-pci -device virtio-keyboard-pci
)
if [ "${LIVE_USB:-0}" = "1" ]; then
  [ -f "$PC_DISK" ] || truncate -s 32G "$PC_DISK"
  ARGS+=(
    -device qemu-xhci,id=xhci
    -drive if=none,id=cle,format=raw,file="$IMAGE"
    -device usb-storage,bus=xhci.0,drive=cle,removable=on,bootindex=1
    -drive file="$PC_DISK",format=raw,if=virtio
  )
elif [ "${INSTALLED:-0}" = "1" ]; then
  ARGS+=(-drive file="$PC_DISK",format=raw,if=virtio)
else
  ARGS+=(-drive file="$IMAGE",format=raw,if=virtio)
fi

if [ "${HEADLESS:-0}" = "1" ]; then
  rm -f out/qemu-monitor.sock out/qmp.sock out/serial.sock out/serial.log
  ARGS+=(
    -device virtio-vga -display none
    -vnc 127.0.0.1:0
    -chardev socket,id=s0,path=out/serial.sock,server=on,wait=off,logfile=out/serial.log
    -serial chardev:s0
    -monitor unix:out/qemu-monitor.sock,server,nowait
    -qmp unix:out/qmp.sock,server,nowait
  )
else
  ARGS+=(-device virtio-vga-gl -display gtk,gl=on)
fi
echo ">> QEMU : ${RAM} Mo, accel=$ACCEL, headless=${HEADLESS:-0}"
exec qemu-system-x86_64 "${ARGS[@]}" "$@"
