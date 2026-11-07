#!/bin/bash
# Test automatique du PREMIER demarrage d'une image, tel que le vivra une
# personne qui demarre la cle USB pour la premiere fois (agrandissement des
# partitions, logo, ecran d'accueil).
#
#   build/50-test-boot.sh [image]     (defaut : out/pristine.img)
#
# L'image n'est pas modifiee : on demarre une copie, dont seule la ligne de
# commande du noyau recoit en plus la console serie (journal dans
# out/serial.log). Echec (code 1) si un service echoue, si le demarrage
# n'aboutit pas, ou si la session graphique n'apparait pas.
# A lancer avant chaque publication.
set -uo pipefail
cd "$(dirname "$0")/.."
SRC=${1:-out/pristine.img}
[ -f "$SRC" ] || { echo "image absente : $SRC"; exit 2; }
TIMEOUT=${TIMEOUT:-600}
S=out/test-boot; mkdir -p "$S/esp"

pkill -f "[q]emu-system-x86" 2>/dev/null; sleep 1
[ -f out/lumina-lite.img ] && [ ! -f "$S/lumina-lite.keep" ] && mv out/lumina-lite.img "$S/lumina-lite.keep"
cp --sparse=always "$SRC" out/lumina-lite.img && truncate -s 16G out/lumina-lite.img
L=$(sudo losetup -fP --show out/lumina-lite.img)
sudo mount "${L}p1" "$S/esp"
# max_level_kmsg=info : sans lui, journald ne recopie sur la console que les
# erreurs, et jamais les messages de reussite que ce test attend.
X="console=ttyS0,115200 console=tty0 loglevel=6 printk.devkmsg=on systemd.journald.forward_to_kmsg=1 systemd.journald.max_level_kmsg=info"
sudo sed -i "/^options/s|\$| $X|" "$S"/esp/loader/entries/*.conf
sudo sed -i "s|\\\$extra; then|\$extra $X; then|" "$S/esp/grub/grub.cfg"
sudo umount "$S/esp"; sudo losetup -d "$L"

T0=$(date +%s)
LIVE_USB=1 HEADLESS=1 build/30-qemu.sh > "$S/qemu.log" 2>&1 &
for _ in $(seq 30); do [ -f out/serial.log ] && break; sleep 1; done
# Reussite = demarrage VALIDE par lumina-boot-ok (bureau affiche et vivant).
# Preuve lue sur le disque, et non sur la console (qui cesse d'etre
# alimentee une fois l'ecran de connexion lance) : en validant, le service
# reecrit la ligne d'options de l'entree de demarrage et n'y garde, de nos
# ajouts, que les console=. Apres l'ecran de connexion, on lui laisse
# AFTER secondes (son attente du bureau va jusqu'a 3 min + 20 s).
AFTER=${AFTER:-420}
RES=1; WHY="ecran de connexion jamais lance en ${TIMEOUT} s"
while [ $(( $(date +%s) - T0 )) -lt "$TIMEOUT" ]; do
  if grep -aq "Starting greetd.service" out/serial.log 2>/dev/null; then RES=0; WHY=""; break; fi
  sleep 3
done
[ "$RES" = 0 ] && sleep "$AFTER"
build/screenshot.sh "$S/ecran.png" >/dev/null 2>&1
LOG=$(sed 's/\x1b\[[0-9;]*m//g' out/serial.log)
FAILED=$(printf '%s\n' "$LOG" | grep -aE "\[FAILED\]|Failed with result" | sort -u)
pkill -f "[q]emu-system-x86" 2>/dev/null; sleep 2
if [ "$RES" = 0 ]; then
  L=$(sudo losetup -rfP --show out/lumina-lite.img)
  sudo mount -o ro "${L}p1" "$S/esp"
  if sudo grep -qs "^options.*printk.devkmsg" "$S"/esp/loader/entries/lumina-a*.conf; then
    RES=1; WHY="demarrage jamais valide (bureau absent ? voir la capture)"
  fi
  sudo umount "$S/esp"; sudo losetup -d "$L"
fi
[ -n "$FAILED" ] && { RES=1; WHY="service(s) en echec"; }

echo "== Premier demarrage de $SRC"
printf '%s\n' "$LOG" | grep -aE "Startup finished" | tail -1
[ -n "$FAILED" ] && printf '%s\n' "$FAILED"
echo "capture : $S/ecran.png"
pkill -f "[q]emu-system-x86" 2>/dev/null; sleep 1
rm -f out/lumina-lite.img
[ -f "$S/lumina-lite.keep" ] && mv "$S/lumina-lite.keep" out/lumina-lite.img
if [ "$RES" = 0 ]; then echo "RESULTAT : OK"; else echo "RESULTAT : ECHEC ($WHY)"; fi
exit "$RES"
