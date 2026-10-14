#!/usr/bin/env bash
# Build one port with mpc-vst-plugins' build_port.sh (CI and local). From the repo root: bash tools/build_ci.sh 9w9
# MPC_VST = a mpc-vst-plugins checkout (default: sibling). Output: ports/<kit>/build/.
set -euo pipefail
cd "$(dirname "$0")/.."
MPC_VST="${MPC_VST:-$PWD/../mpc-vst-plugins}"
if [ "$1" = trkit ]; then
  bash tools/apply_trkit_patches.sh
  # optional skin style: bash tools/build_ci.sh trkit <restyle|editor|full> (default: the committed layout.conf)
  [ -z "${2:-}" ] || { bash tools/gen_skins_docker.sh gen_trmpc_skin.py >/dev/null && bash tools/gen_skins_docker.sh gen_trmpc_full.py >/dev/null && cp "ports/trkit/layouts/$2.conf" ports/trkit/layout.conf; }
fi
bash "$MPC_VST/tools/build_port.sh" "ports/$1/vst.json"
