#!/usr/bin/env bash
# Build all four ports (armhf .so + skin), run the offline test, render skin previews. Run it from WSL/Linux with Docker.
# mpc-vst-plugins' tools and Docker builds need a path without spaces, so work on a copy under /tmp:
#   bash tools/build_all.sh [kit ...]      (default: 6w6 8w8 cw78 9w9 trkit)
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
PLUG="${MPC_VST_PLUGINS:-$SRC/../mpc-vst-plugins}"
W=${TRBUILD_DIR:-$HOME/.cache/trbuild}/run.$(date +%s); mkdir -p $W   # fresh dir each run: docker leaves root-owned files behind
cp -r "$PLUG" $W/p
mkdir $W/t; tar -C "$SRC" --exclude=_build --exclude=.git --exclude='ports/*/build' -cf - . | tar -C $W/t -xf -
find $W/p -type f \( -name '*.py' -o -name '*.sh' \) -exec sed -i 's/\r$//' {} +
KITS="${*:-6w6 8w8 cw78 9w9 trkit}"
cd $W/p
for k in $KITS; do
  echo "=== $k"
  bash tools/test_port.sh ../t/ports/$k/vst.json 2>&1 | grep -E '^(FAIL|PASSED)|error' || true
  timeout 1800 bash tools/build_port.sh ../t/ports/$k/vst.json 2>&1 | grep -E 'exported|glibc|error|Traceback' || true
  [ "$k" = trkit ] && continue
  docker run --rm -v $W:/w -w /w/p python:3.11-slim sh -c "pip install -q pillow >/dev/null 2>&1; d=\$(ls -d /w/t/ports/$k/build/skin/*/ | head -n 1); mkdir -p /w/t/ports/$k/build/preview; rm -f /w/t/ports/$k/build/preview/*.png; python3 tools/studio.py preview \"\${d}Plugin Skins\" -o /w/t/ports/$k/build/preview/page_%d.png" | tail -n 1
done
for k in $KITS; do
  mkdir -p "$SRC/_build/$k"; rm -rf "$SRC/_build/$k/skin" "$SRC/_build/$k/preview"
  cp -r $W/t/ports/$k/build/skin "$SRC/_build/$k/skin" 2>/dev/null || true
  if [ -d $W/t/ports/$k/build/preview ]; then   # one image per Q-Link set; keep one per distinct page
    mkdir -p "$SRC/_build/$k/preview"
    for f in $(ls $W/t/ports/$k/build/preview/page_*.png | sort -V); do
      h=$(md5sum "$f" | cut -d' ' -f1); [ -e "$W/seen_$h" ] && continue; touch "$W/seen_$h"
      cp "$f" "$SRC/_build/$k/preview/$(basename "$f")"
    done
  fi
  cp $W/t/ports/$k/build/*.so $W/t/ports/$k/build/pluginlist-entry.xml "$SRC/_build/$k/" 2>/dev/null || true
done
echo done
