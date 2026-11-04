#!/bin/bash
# Lance une commande dans la session graphique de la VM, avec l'environnement
# exact du bureau (affichage Wayland, bus de session, dossier personnel...),
# lu par root dans le processus lumina-shell puis transmis a lumina.
# Usage : build/vm-launch.sh pcmanfm
cd "$(dirname "$0")/.."
exec build/vm-run.sh "cd /var/lib/lumina/home && P=\$(pgrep -o -x lumina-shell) && xargs -0 -a /proc/\$P/environ sh -c 'exec runuser -u lumina -- env -i \"\$@\" setsid -f $1 >/dev/null 2>&1' sh && echo lance" 60
