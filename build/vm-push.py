#!/usr/bin/env python3
"""Copie un fichier dans la VM (lancee avec HEADLESS=1).
Usage : build/vm-push.py fichier_local chemin_dans_la_vm

Le contenu passe par le reseau virtuel de QEMU : la VM joint l'hote a
l'adresse 10.0.2.2, et lit le fichier avec la redirection /dev/tcp de
bash (aucun outil a installer dans l'image). Taper le contenu dans la
console serie, lui, perd des caracteres au-dela de quelques Ko."""
import hashlib, os, socket, subprocess, sys, threading
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0)); srv.listen(1)
port = srv.getsockname()[1]
def serve():
    c, _ = srv.accept(); c.sendall(data); c.close()
threading.Thread(target=serve, daemon=True).start()

cmd = (f"cat < /dev/tcp/10.0.2.2/{port} > /tmp/vm-push.tmp && "
       f"install -m 0755 /tmp/vm-push.tmp {dst} && md5sum {dst} | cut -c1-8")
out = subprocess.run(["build/vm-run.sh", cmd, "120"], capture_output=True, text=True).stdout.strip()
local = hashlib.md5(data).hexdigest()[:8]
ok = out.endswith(local)
print(f"copie {'OK' if ok else 'ECHEC'} : {src} -> {dst} ({len(data)} octets, md5 {local}, vm : {out[-8:] or '?'})")
sys.exit(0 if ok else 1)
