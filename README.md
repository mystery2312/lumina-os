<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo-dark.svg">
    <img src="docs/logo.svg" width="96" alt="Logo LuminaOS : un soleil qui se lève sur l'horizon">
  </picture>
</p>

# LuminaOS

Un OS de type ChromeOS : le navigateur **est** le système. Cible assumée :
**2 Go de RAM, CPU Atom/Celeron, eMMC lente**. Qui peut le moins peut le plus.

## Télécharger

Dernière version : **[Releases](https://github.com/mystery2312/lumina-os/releases/latest)**,
fichier `lumina-os-<version>.img.xz` à écrire sur une clé USB de 16 Go ou
plus (balenaEtcher, Rufus en mode DD, GNOME Disques). Voir
[Essayer, puis installer](#essayer-puis-installer).

## Philosophie

Reproduire les *fonctionnalités* de Google Aluminium — apps Android,
assistant IA, compte unique, mises à jour atomiques — sans en reprendre
l'*implémentation*. Aluminium empile le framework Android et Chromium :
3 à 4 Go de RAM au repos. Ici chaque brique lourde est **chargée à la
demande** et jamais résidente.

| | Aluminium | LuminaOS |
|---|---|---|
| Base | Kernel + framework Android | Debian 13 minimal |
| Bureau | SystemUI Android | `labwc` (fenêtres) + `lumina-shell` (C/GTK3, ~35 Mo) |
| Apps Android | Résident (ART, SystemServer) | Waydroid à la demande, dès 4 Go de RAM |
| IA | Gemini Nano on-device | Cloud par défaut, modèle local optionnel |
| RAM, bureau + Chromium ouvert | ~3,5 Go | **~490 Mo mesurés** (VM 2 Go) |

## Construire

```bash
build/00-deps.sh     # installe les outils de construction (sudo)
build/build.sh       # lumina-shell + rootfs + image
build/30-qemu.sh     # démarre avec 2 Go de RAM (fenêtre graphique)
```

`BUILD_MODE=root` (défaut) passe par sudo ; `BUILD_MODE=unshare` construit
sans aucun privilège.

### Tester sans écran (serveur, SSH)

```bash
HEADLESS=1 build/30-qemu.sh &          # VM sans fenêtre, écran en VNC
                                       # sur 127.0.0.1:5900 (voir ci-dessous)
build/screenshot.sh out/ecran.png      # capture de l'écran de la VM
build/vm-run.sh "free -m"              # commande dans la VM (console série)
build/vm-push.py fichier /chemin/vm    # copie un fichier dans la VM
build/vm-restart-shell.sh              # relance lumina-shell sans redémarrer
build/vm-key.sh meta_l-up f11          # touches clavier (agrandir, plein écran…)
build/vm-click.sh 640 400 droit        # clic souris (gauche, droit, double)
build/vm-launch.sh lumina-files        # lance une appli dans la session
```

Pour voir et utiliser la VM depuis un autre ordinateur, ouvrir un tunnel
SSH puis pointer un client VNC (TigerVNC, RealVNC…) sur `localhost:5900` :

```bash
ssh -L 5900:127.0.0.1:5900 utilisateur@serveur
```

Sans KVM, la VM est très lente (plusieurs minutes pour une page Chromium) :
elle sert à vérifier l'interface, pas à juger la fluidité. `RAM=1536`
ménage un hôte qui n'a que 4 Go.

`RAM=1024 build/30-qemu.sh` pour éprouver le pire cas.

L'image ne monte jamais rien : `mke2fs -d` fabrique l'ext4 depuis un
répertoire, `mcopy` remplit l'ESP, `sgdisk` écrit la GPT dans un simple
fichier.

## Le bureau

`src/lumina-shell/` : un seul processus GTK3 + gtk-layer-shell qui dessine
le fond d'écran, la barre d'état, l'étagère, le tiroir d'applications et
les réglages rapides.

- **Barre d'état** (haut, 28 px) : lanceur à gauche, heure, réglages
  rapides à droite. Transparente sur le fond d'écran, elle devient opaque,
  un cran plus foncée que le cadre des fenêtres, dès qu'une fenêtre est
  agrandie.
- **Étagère** (bas, pleine largeur, façon ChromeOS) : applis épinglées et
  applis ouvertes, un point sous celles qui ont une fenêtre, un trait sous
  l'active. Clic : lancer, ramener la fenêtre, ou la réduire si elle est
  déjà active. Les fenêtres sont suivies par le protocole Wayland
  `wlr-foreign-toplevel-management`.
- **Icônes** : les applis installées gardent leur icône. Celles des applis
  web LuminaOS (Gmail, Drive, YouTube…) ne sont **pas** distribuées dans
  l'image (marques de leurs propriétaires) : `lumina-fetch-icons` les
  télécharge sur l'appareil à la première connexion, depuis l'adresse
  officielle du service. Hors ligne, un glyphe LuminaOS les remplace. Toutes les valeurs sont lues dans
le système (batterie, Wi-Fi via iwd, volume via PipeWire, luminosité,
RAM et zram) ; un appareil absent est masqué, jamais simulé.

- Touche Windows ou bouton en haut à gauche : tiroir d'applications
- Windows + A ou clic sur l'heure/batterie : réglages rapides
- Windows + flèches : ancrer / agrandir · Alt + Tab : changer de fenêtre
- Applis épinglées dans l'étagère : `/etc/lumina/dock.conf` · thème : `~/.config/lumina/theme`
- Applis web (Mail, Docs…) : `/usr/local/share/applications/lumina-*.desktop`,
  ouvertes en fenêtre dédiée par un Chromium partagé

### Fichiers, Terminal et Linux

- **Fichiers** (`src/lumina-files`) : gestionnaire de fichiers maison, dans
  le style Material 3 de l'appli Fichiers d'Android / Aluminium, même
  palette et même thème que le bureau (`src/common/lumina-ui`). Navigation
  à gauche (dossiers usuels, clés USB avec éjection, corbeille), chemin en
  pastilles, recherche, grille ou liste, miniatures d'images, menu clic
  droit, copier / couper / coller / renommer en arrière-plan, corbeille
  avec « Annuler » et restauration. Fenêtre aux quatre coins arrondis.
  Raccourcis : Ctrl+C/X/V, Suppr (Maj+Suppr : définitif), F2, Ctrl+H
  (fichiers cachés), Ctrl+Maj+N (dossier), Ctrl+F, Alt+←/→/↑. PDF, images,
  vidéos et musique s'ouvrent dans le navigateur.
- **Dossier personnel** : `/var/lib/lumina/home`, sur la partition d'état.
  Les mises à jour A/B remplacent la partition système entière ; les
  fichiers de l'utilisateur n'y sont donc jamais.
- **Terminal** (`foot`) : ouvre **Linux**, un conteneur Debian comme
  Crostini sur ChromeOS (Distrobox + Podman sans privilèges). Installé au
  premier lancement. On y installe ce qu'on veut avec
  `sudo apt install …` sans toucher au système LuminaOS. Le dossier
  personnel est partagé, et les applis graphiques installées apparaissent
  d'elles-mêmes dans le tiroir (et en disparaissent à la désinstallation).
  `lumina-linux --remove` supprime le conteneur.

### Applis installées

Le tiroir se met à jour **en direct**, sans redémarrer la session, et
affiche l'**icône propre** de chaque appli installée :

- **PWA** : « Installer l'application » dans Chromium. Chromium dépose un
  `.desktop` et son icône dans `~/.local/share` ; type affiché « Web ».
  Les réglages de Chromium (`/etc/chromium.d/lumina`) s'appliquent aussi
  aux PWA lancées alors que le navigateur est fermé.
- **Android** (machines de **4 Go de RAM ou plus**) :
  `sudo lumina-android --install`. Chaque appli installée dans Android
  apparaît dans le tiroir (type « Android ») ; Android ne démarre qu'à
  l'ouverture de la première appli. Sous 4 Go, l'installation est refusée :
  Android (~500 Mo actif) ralentirait tout le système.
- **Paquets Debian** avec interface graphique : type « Linux ».

**Sans accélération 3D** (vieux GPU non pris en charge, VM) :
`lumina-session` interroge le GPU avant de démarrer le compositeur, à
chaque ouverture de session (`eglinfo`, sans écran) : émulation llvmpipe
→ rendu logiciel `pixman` pour labwc et `--disable-gpu` pour Chromium.
Réglage manuel dans `/etc/lumina/session.conf`.

## Essayer, puis installer

L'image démarre **depuis une clé USB** (16 Go minimum) sans rien modifier
sur le PC : PC récents en UEFI, **Secure Boot activé ou non** (shim signé
par Microsoft → systemd-boot et noyau signés par Debian), et vieux PC sans
UEFI (BIOS : GRUB, même logique A/B). Écrire
l'image sur la clé : Rufus en mode DD ou balenaEtcher (Windows, Mac),
Etcher ou GNOME Disques (Linux).

Au démarrage sur la clé, une fenêtre propose **Essayer LuminaOS** ou
**Installer sur ce PC** ; l'appli « Installer LuminaOS » reste dans le
tiroir tant qu'on tourne depuis la clé. L'installateur (`lumina-install`,
écriture par `lumina-install-disk`) :

- liste les disques internes avec leur contenu, sans la clé elle-même ;
- exige une confirmation explicite : **tout le disque choisi est effacé** ;
- crée ESP 512 Mo + ROOT-A 4 Go + ROOT-B 4 Go + STATE (le reste), avec des
  identifiants de partitions **tirés au hasard** (la clé et le disque
  installé ne se confondent jamais au démarrage) ;
- copie le système (sans les fichiers de l'essai), installe shim +
  systemd-boot et les enregistre dans le firmware (`efibootmgr`), avec le
  chemin de secours `EFI/BOOT/BOOTX64.EFI`, et GRUB pour les PC en BIOS.

Une fois installé, l'installateur disparaît (`/etc/lumina/installed`).
La clé reste utilisable pour essayer ou installer ailleurs.

Tester dans la VM : `LIVE_USB=1 HEADLESS=1 build/30-qemu.sh` branche
l'image comme une clé USB avec un disque interne vierge de 32 Go ; après
installation, `INSTALLED=1 HEADLESS=1 build/30-qemu.sh` démarre ce disque
seul.

## Mises à jour automatiques (A/B)

Une machine installée cherche une nouvelle version 5 minutes après le
démarrage puis toutes les 6 heures (`lumina-update.timer`), avec une
priorité minimale pour rester utilisable :

1. lecture du manifeste de la dernière Release GitHub
   (`releases/latest/download/manifest.txt`) et **vérification de sa
   signature** (minisign, clé publique `/etc/lumina/update.pub`) : seule
   la clé de LuminaOS peut annoncer une version ;
2. téléchargement de l'image système compressée, écrite dans la partition
   système **inactive** (B si l'on tourne sur A), empreinte SHA-256 vérifiée
   au passage ; l'entrée de démarrage de cette partition est retirée
   *avant* d'écrire, une écriture interrompue n'est donc jamais démarrée ;
3. recopie de ce qui est propre à la machine (fstab, identifiant, marqueur
   d'installation), noyau dans `ESP/lumina-<a|b>/`, entrée de démarrage
   avec **3 essais** ;
4. « Mise à jour prête · Redémarrer » dans les réglages rapides.

Au redémarrage, systemd-boot prend la version la plus récente. Elle est
validée (`systemd-bless-boot`) quand le bureau tourne depuis 20 s ; sinon
`panic=10` la fait redémarrer, et après 3 échecs **l'ancienne version
reprend la main** toute seule. Les fichiers, Linux, Chromium et les réseaux
Wi-Fi sont sur la partition d'état : une mise à jour n'y touche jamais.
Les réglages propres à une machine vont dans
`/var/lib/lumina/config/session.conf`.

Publier une version :

```bash
DEV=0 build/10-rootfs.sh            # image sans accès root
build/40-release.sh                 # image compressée + manifest.txt signé
build/40-release.sh --publish       # Release GitHub (refusé si DEV=1)
```

La clé privée de signature (`~/.config/lumina-os/update.key`, ou
`LUMINA_SIGN_KEY`) n'est **jamais** dans le dépôt : à sauvegarder à part.
Sans elle, plus aucune mise à jour ne peut être publiée pour les machines
existantes.

## Disposition du disque (calquée sur ChromeOS)

```
p5  BIOS       1 Mo  -       GRUB pour les PC sans UEFI (secteurs 34-2047)
p1  ESP      128 Mo  FAT32   shim, systemd-boot, GRUB, vmlinuz + initrd
p2  ROOT-A  3072 Mo  ext4    système (lecture seule et vérifié : jalon M3b)
p3  ROOT-B  3072 Mo  ext4    cible des mises à jour A/B
p4  STATE   le reste ext4    /var/lib/lumina : profil, réglages, données
```

Une mise à jour s'écrit dans la racine inactive puis bascule l'entrée de
boot. Si le nouveau système ne démarre pas, on repart sur l'ancien : pas
de machine cassée à distance.

## Feuille de route

- [x] **M0** Chaîne de build, image GPT amorçable
- [x] **M1** Session graphique, réglages mémoire 2 Go, détection GPU
- [x] **M2a** Bureau façon Aluminium : barre d'état, barre de tâches,
      tiroir, réglages rapides, thème sombre, applis web en fenêtres
- [x] **M2b** Étagère façon ChromeOS avec applis ouvertes, barre d'état fine
      qui s'opacifie, vraies icônes des applis
- [x] **M2c** Fichiers, Terminal, Linux en conteneur façon Crostini
- [x] **M2d** Premier démarrage (langue, Wi-Fi, compte), verrouillage,
      Réglages, Bluetooth, thème sombre jusque dans Chromium, notifications,
      captures d'écran
- [x] **M2e** Secure Boot, démarrage BIOS des PC pré-UEFI
- [ ] **M2f** Coins arrondis en bas des fenêtres (limite de labwc)
- [x] **M3** Mises à jour A/B depuis GitHub, retour automatique, testé
- [x] **M3a** Mises à jour signées (minisign)
- [ ] **M3b** Racine en lecture seule et vérifiée (dm-verity)
- [ ] **M4** Apps Android : lanceur et tiroir prêts, à valider sur une vraie
      machine de 4 Go (Waydroid ne tourne pas dans la VM de test)
- [ ] **M5** Couche IA : API distante par défaut, `llama.cpp` + modèle 0,6 B
      en option hors ligne

## Mot de passe root

Images de développement (`DEV=1`, par défaut) : `root` / `lumina`, console
connectée sur Alt+F2. Images publiées (`DEV=0`) : aucun accès root ;
`build/40-release.sh --publish` refuse une image de développement.

## Licence

LuminaOS est un logiciel libre, distribué sous licence
[GPL-3.0](LICENSE) : chacun peut l'utiliser, l'étudier, le modifier et le
redistribuer, à condition que les versions modifiées restent libres.
Les logiciels intégrés à l'image (Debian, Chromium, labwc…) gardent leurs
propres licences.
