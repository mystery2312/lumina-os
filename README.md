# LuminaOS

Un OS de type ChromeOS : le navigateur **est** le système. Cible assumée :
**2 Go de RAM, CPU Atom/Celeron, eMMC lente**. Qui peut le moins peut le plus.

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
`lumina-shell --probe-gl` le détecte à l'ouverture de session ; Chromium
passe alors en `--disable-gpu` et le compositeur en rendu `pixman` au
démarrage suivant. Réglage manuel dans `/etc/lumina/session.conf`.

## Disposition du disque (calquée sur ChromeOS)

```
p1  ESP      128 Mo  FAT32   systemd-boot + vmlinuz + initrd
p2  ROOT-A  3072 Mo  ext4    système, monté en lecture seule (jalon M3)
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
- [ ] **M2c** Coins arrondis en bas des fenêtres (limite de labwc), Chromium
      qui suit le thème sombre, vrai gestionnaire de fichiers, Bluetooth,
      boot BIOS pour les netbooks pré-UEFI
- [ ] **M3** Racine en lecture seule, dm-verity, updater A/B
- [ ] **M4** Apps Android : lanceur et tiroir prêts, à valider sur une vraie
      machine de 4 Go (Waydroid ne tourne pas dans la VM de test)
- [ ] **M5** Couche IA : API distante par défaut, `llama.cpp` + modèle 0,6 B
      en option hors ligne

## Mot de passe root

`root` / `lumina`, autologin sur Alt+F2, pour le développement.
Défini dans `overlay/usr/local/sbin/lumina-setup` — **à retirer avant toute
diffusion d'image**.

## Licence

LuminaOS est un logiciel libre, distribué sous licence
[GPL-3.0](LICENSE) : chacun peut l'utiliser, l'étudier, le modifier et le
redistribuer, à condition que les versions modifiées restent libres.
Les logiciels intégrés à l'image (Debian, Chromium, labwc…) gardent leurs
propres licences.
