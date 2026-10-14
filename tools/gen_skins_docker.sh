#!/usr/bin/env bash
# Run the skin generators where fontTools is available (they outline lettering from ../mpc-vst/Assets/fonts):
#   bash tools/gen_skins_docker.sh gen_trmpc_skin.py      (or gen_drum_skins.py / make_layout.py)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONTS="$(cd "$ROOT/../mpc-vst/Assets/fonts" 2>/dev/null && pwd || cd "$ROOT/../mpc-vst-plugins/Assets/fonts" && pwd)"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e FONT_SRC=/fonts -v "$ROOT":/w -v "$FONTS":/fonts:ro -w /w python:3.11-slim \
  sh -c "pip install -q --target /tmp/p fonttools >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 tools/$1"
