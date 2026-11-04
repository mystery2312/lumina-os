#!/usr/bin/env python3
"""Execute une commande dans la VM via la console serie et affiche sa sortie.
Usage : build/vm-run.sh "commande" [timeout_s]
Se connecte en root (mot de passe de dev) si necessaire."""
import socket, sys, time, re, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
cmd = sys.argv[1]; tmo = float(sys.argv[2]) if len(sys.argv) > 2 else 120
s = socket.socket(socket.AF_UNIX); s.connect("out/serial.sock"); s.settimeout(0.5)
buf = b""
def read_until(pat, t):
    global buf; end = time.time() + t
    while time.time() < end:
        try:
            d = s.recv(65536)
            if d: buf += d
        except socket.timeout: pass
        if re.search(pat, buf): return True
    return False
def send(x): s.sendall(x.encode() + b"\r")
send("")
if read_until(rb"login: $", 8):
    send("root"); read_until(rb"assword: ", 10); send("lumina")
    read_until(rb"# $", 30)
buf = b""
mark = "FIN%d" % int(time.time())
# Echo coupe AVANT d'envoyer la commande : sa sortie commence alors
# directement, sans ligne d'echo a sauter (qui avalait la 1re ligne utile).
send("stty -echo cols 250; export TERM=dumb SYSTEMD_COLORS=0 PAGER=cat; echo P''RET")
read_until(rb"PRET\r?\n", 10); read_until(rb"# $", 2); buf = b""
# Le marqueur est coupe en deux dans la commande (F''IN...) : son echo
# eventuel ne peut pas etre confondu avec la vraie fin de sortie.
send(f"({cmd}) 2>&1; echo {mark[0]}''{mark[1:]}")
ok = read_until(mark.encode() + rb"\r?\n", tmo)
out = buf.decode(errors="replace")
out = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", out)
out = out.split(mark)[0] if ok else out
print(out.strip()); print("" if ok else "[TIMEOUT]")
