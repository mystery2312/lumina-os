#!/bin/bash
# Capture l'ecran de la VM lancee avec HEADLESS=1 -> out/screen.png
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-out/screen.png}"
rm -f out/screen.ppm
# 2e argument : numero de l'ecran (0, 1...) quand la VM en a plusieurs
HEAD=""; [ -n "${2:-}" ] && HEAD=" vga$2"
printf 'screendump %s/out/screen.ppm%s\n' "$PWD" "$HEAD" | socat - UNIX-CONNECT:out/qemu-monitor.sock >/dev/null
for _ in $(seq 20); do [ -s out/screen.ppm ] && break; sleep 0.3; done
python3 -c "
import sys
d=open('out/screen.ppm','rb').read()
# PPM P6 -> PNG minimal, sans dependance
import zlib,struct
p=d.split(b'\n',3); w,h=map(int,p[1].split()); px=p[3]
raw=b''.join(b'\x00'+px[y*w*3:(y+1)*w*3] for y in range(h))
c=lambda t,b:struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
open('$OUT','wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+c(b'IDAT',zlib.compress(raw,6))+c(b'IEND',b''))
print('$OUT',w,'x',h)
"
