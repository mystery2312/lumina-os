#!/usr/bin/env python3
"""Glisse-depose dans la VM (lancee avec HEADLESS=1), par QMP.
Usage : build/vm-drag.sh x1 y1 x2 y2"""
import json, os, socket, sys, time
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
W, H, MAX = 1280, 800, 32767
x1, y1, x2, y2 = map(int, sys.argv[1:5])
s = socket.socket(socket.AF_UNIX); s.connect("out/qmp.sock"); f = s.makefile("rw")
f.readline()
def qmp(cmd, args=None):
    f.write(json.dumps({"execute": cmd, **({"arguments": args} if args else {})}) + "\n"); f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r: return r
qmp("qmp_capabilities")
def ev(events): qmp("input-send-event", {"events": events})
def move(x, y): ev([{"type": "abs", "data": {"axis": "x", "value": x * MAX // W}},
                    {"type": "abs", "data": {"axis": "y", "value": y * MAX // H}}])
move(x1, y1); time.sleep(0.3)
ev([{"type": "btn", "data": {"down": True, "button": "left"}}]); time.sleep(0.2)
N = 25
for i in range(1, N + 1):
    move(x1 + (x2 - x1) * i // N, y1 + (y2 - y1) * i // N); time.sleep(0.04)
time.sleep(0.4)
ev([{"type": "btn", "data": {"down": False, "button": "left"}}])
