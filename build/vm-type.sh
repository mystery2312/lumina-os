#!/bin/bash
# Tape une commande dans la VM (lancee avec HEADLESS=1) via le moniteur QEMU.
# Usage : build/vm-type.sh "commande"   (Entree ajoutee automatiquement)
set -euo pipefail
cd "$(dirname "$0")/.."
send(){ printf 'sendkey %s\n' "$1" | socat - UNIX-CONNECT:out/qemu-monitor.sock >/dev/null; sleep 0.05; }
s="$1"
for ((i=0;i<${#s};i++)); do
  c="${s:i:1}"
  case "$c" in
    [a-z0-9]) send "$c";; [A-Z]) send "shift-${c,,}";;
    ' ') send spc;; '-') send minus;; '_') send shift-minus;; '.') send dot;;
    '/') send slash;; '|') send shift-backslash;; '>') send shift-dot;; '<') send shift-comma;;
    '=') send equal;; ',') send comma;; ':') send shift-semicolon;; ';') send semicolon;;
    '*') send shift-8;; '"') send shift-apostrophe;; "'") send apostrophe;; '&') send shift-7;;
    '@') send shift-2;; '+') send shift-equal;; '(') send shift-9;; ')') send shift-0;; '$') send shift-4;;
    '~') send shift-grave_accent;; '!') send shift-1;;
    *) echo "caractere non gere: $c" >&2; exit 1;;
  esac
done
send ret
