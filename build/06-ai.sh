#!/bin/bash
# Moteur d'IA locale de Lumi : llama.cpp (serveur seulement), binaire
# officiel du projet, version FIGEE et verifiee par empreinte SHA-256.
# Rien ne s'execute ici : on extrait les fichiers utiles dans out/ai/,
# que 10-rootfs.sh ajoute a l'image (/usr/lib/lumina-ai).
# Le modele, lui, n'est PAS dans l'image : il est telecharge (et verifie)
# seulement si l'utilisateur active Lumi (voir lumina-ai-model).
set -euo pipefail
cd "$(dirname "$0")/.."
TAG=b11765
SHA256=2a8d3b1c1777dcddeff1aa626979d7a7b3ea8ae1358f488763874aa95093cdee
URL=https://github.com/ggml-org/llama.cpp/releases/download/$TAG/llama-$TAG-bin-ubuntu-x64.tar.gz
C=out/cache/llama-$TAG
DST=out/ai/usr/lib/lumina-ai

mkdir -p "$C"
if ! echo "$SHA256  $C/llama.tar.gz" | sha256sum -c --status 2>/dev/null; then
  echo ">> telechargement de llama.cpp $TAG"
  curl -fsSL -o "$C/llama.tar.gz.part" "$URL"
  mv "$C/llama.tar.gz.part" "$C/llama.tar.gz"
fi
echo "$SHA256  $C/llama.tar.gz" | sha256sum -c --quiet

rm -rf out/ai "$C/x"; mkdir -p "$DST" "$C/x"
tar -xzf "$C/llama.tar.gz" -C "$C/x"
S="$C/x/llama-$TAG"
# Serveur + bibliotheques ; toutes les variantes CPU (de SSE4.2 aux plus
# recentes) : llama.cpp choisit au lancement la meilleure pour le PC.
cp -a "$S"/llama-server "$S"/libllama-server-impl.so "$S"/libllama.so* "$S"/libllama-common.so* \
      "$S"/libmtmd.so* "$S"/libggml.so* "$S"/libggml-base.so* "$S"/libggml-cpu-*.so "$DST/"
cp "$S/LICENSE" "$DST/LICENSE-llama.cpp"
echo "$TAG" > "$DST/VERSION"
echo ">> lumina-ai : llama.cpp $TAG, $(du -sh "$DST" | cut -f1)"
