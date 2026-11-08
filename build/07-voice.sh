#!/bin/bash
# Moteurs de la voix de Lumi, ajoutes a l'image dans /usr/lib/lumina-ai :
#   whisper/ : whisper.cpp (comprendre), compile ici, toutes variantes CPU
#              (la meilleure est choisie au lancement : du Core 2 a l'AVX2) ;
#   piper/   : Piper (parler), binaire officiel.
# Versions FIGEES et verifiees par empreinte SHA-256. Les modeles ne sont
# pas dans l'image : voir /usr/share/lumina/ai-voice.conf.
set -euo pipefail
cd "$(dirname "$0")/.."
WTAG=v1.9.5
WSHA=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
PTAG=2023.11.14-2
PSHA=a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992
C=out/cache/voice
DST=out/ai/usr/lib/lumina-ai

fetch() {  # fetch <fichier> <sha> <url>
  echo "$2  $1" | sha256sum -c --status 2>/dev/null && return 0
  echo ">> telechargement de $3"
  curl -fsSL --retry 8 --retry-delay 15 --retry-all-errors -o "$1.part" "$3"
  mv "$1.part" "$1"
  echo "$2  $1" | sha256sum -c --quiet
}
mkdir -p "$C"
fetch "$C/whisper-$WTAG.tar.gz" "$WSHA" "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/$WTAG.tar.gz"
fetch "$C/piper-$PTAG.tar.gz" "$PSHA" "https://github.com/rhasspy/piper/releases/download/$PTAG/piper_linux_x86_64.tar.gz"

W=$C/whisper-$WTAG
if [ ! -x "$W/inst/whisper-cli" ]; then
  rm -rf "$W"; mkdir -p "$W"
  tar -xzf "$C/whisper-$WTAG.tar.gz" -C "$W" --strip-components=1
  cmake -S "$W" -B "$W/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DGGML_BACKEND_DL=ON \
        -DGGML_CPU_ALL_VARIANTS=ON -DGGML_NATIVE=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF \
        -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN' >/dev/null
  nice -n 19 cmake --build "$W/build" -j"$(nproc)" --target whisper-cli >/dev/null
  mkdir -p "$W/inst"
  cp -a "$W/build/bin/"* "$W/inst/" 2>/dev/null || true
  find "$W/build" -name '*.so*' -exec cp -a {} "$W/inst/" \;
  find "$W/inst" -type f \( -name 'whisper-cli' -o -name '*.so*' \) -exec strip {} + 2>/dev/null || true
  cp "$W/LICENSE" "$W/inst/LICENSE-whisper.cpp"
fi
rm -rf "$DST/whisper" "$DST/piper"
mkdir -p "$DST/whisper" "$DST/piper"
cp -a "$W/inst/." "$DST/whisper/"
tar -xzf "$C/piper-$PTAG.tar.gz" -C "$DST/piper" --strip-components=1
echo ">> voix : whisper $(du -sh "$DST/whisper" | cut -f1), piper $(du -sh "$DST/piper" | cut -f1)"
