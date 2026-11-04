#!/bin/bash
# Demarre l'image dans QEMU. Par defaut 2 Go de RAM : c'est la cible.
#
#   build/30-qemu.sh             fenetre graphique (necessite un ecran)
#   HEADLESS=1 build/30-qemu.sh  sans ecran : journal serie dans out/serial.log,
#                                terminal via build/vm-run.sh, captures via
#                                build/screenshot.sh
set -euo pipefail
cd "$(dirname "$0")/.."
source config/image.conf
RAM="${RAM:-2048}"
OVMF=/usr/share/OVMF/OVMF_CODE_4M.fd
[ -f "$OVMF" ] || { echo "OVMF introuvable : sudo apt install ovmf"; exit 1; }
VARS=out/OVMF_VARS.fd
[ -f "$VARS" ] || cp /usr/share/OVMF/OVMF_VARS_4M.fd "$VARS"
ACCEL=$([ -w /dev/kvm ] && echo kvm || echo tcg)

ARGS=(
  -machine q35,accel=$ACCEL -cpu max -smp 2 -m "$RAM"
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF"
  -drive if=pflash,format=raw,unit=1,file="$VARS"
  -drive file="$IMAGE",format=raw,if=virtio
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0
  -device virtio-tablet-pci -device virtio-keyboard-pci
)
if [ "${HEADLESS:-0}" = "1" ]; then
  rm -f out/qemu-monitor.sock out/serial.sock out/serial.log
  ARGS+=(
    -device virtio-vga -display none
    -chardev socket,id=s0,path=out/serial.sock,server=on,wait=off,logfile=out/serial.log
    -serial chardev:s0
    -monitor unix:out/qemu-monitor.sock,server,nowait
  )
else
  ARGS+=(-device virtio-vga-gl -display gtk,gl=on)
fi
echo ">> QEMU : ${RAM} Mo, accel=$ACCEL, headless=${HEADLESS:-0}"
exec qemu-system-x86_64 "${ARGS[@]}" "$@"
