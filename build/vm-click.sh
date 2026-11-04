#!/usr/bin/env python3
"""Clique dans la VM (lancee avec HEADLESS=1) a une position en pixels.
Usage : build/vm-click.sh x y [gauche|droit|double]

Passe par QMP (input-send-event) : seul moyen d'envoyer une position
ABSOLUE a la tablette virtuelle ; la commande souris du moniteur QEMU
n'envoie que des deplacements relatifs, que la tablette ignore."""
import json, os, socket, sys, time
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
W, H, MAX = 1280, 800, 32767
x, y = int(sys.argv[1]), int(sys.argv[2])
mode = sys.argv[3] if len(sys.argv) > 3 else "gauche"
s = socket.socket(socket.AF_UNIX); s.connect("out/qmp.sock"); f = s.makefile("rw")
f.readline()
def qmp(cmd, args=None):
    f.write(json.dumps({"execute": cmd, **({"arguments": args} if args else {})}) + "\n"); f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r: return r
qmp("qmp_capabilities")
def ev(events): qmp("input-send-event", {"events": events})
ev([{"type": "abs", "data": {"axis": "x", "value": x * MAX // W}},
    {"type": "abs", "data": {"axis": "y", "value": y * MAX // H}}])
time.sleep(0.3)
btn = "right" if mode == "droit" else "left"
for _ in range(2 if mode == "double" else 1):
    ev([{"type": "btn", "data": {"down": True, "button": btn}}]); time.sleep(0.06)
    ev([{"type": "btn", "data": {"down": False, "button": btn}}]); time.sleep(0.08)
