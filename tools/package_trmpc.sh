#!/usr/bin/env bash
# Package TR-MPC and its Tap FX plugin as two zips (release.py makes one plugin per zip) and optionally install them.
#   bash tools/package_trmpc.sh <version> <dist dir> [device ip]
# Needs the three builds (bash tools/build_ci.sh trkit [full]; build_port.sh ports/trkit/tapfx/vst.json).
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="$1"; DIST="$2"; DEVICE="${3:-}"
MV="${MPC_VST:-$PWD/../mpc-vst-plugins}"
B=ports/trkit/build
mkdir -p "$DIST"
python3 "$MV/tools/release.py" --so $B/trmpc.so --skin "$B/skin/sd88me - VST - TR-MPC" --entry $B/pluginlist-entry.xml \
  --version "$VERSION" --extra ports/9w9/src/samples:engine/samples --bench ports/trkit/bench.txt --license GPL-3.0-only \
  --id tr-mpc --repo sd88me/mpc-vst-TR-MPC -o "$DIST"
ZIPS=$(ls "$DIST"/TR-MPC-"$VERSION"-*.zip)
for t in "tapfx:trmpc_tapfx:TR-MPC Tap FX:tr-mpc-tap-fx:effect"; do
  IFS=: read -r dir so name id kind <<<"$t"
  python3 "$MV/tools/release.py" --so "ports/trkit/$dir/build/$so.so" --skin "ports/trkit/$dir/build/skin/sd88me - VST - $name" \
    --entry "ports/trkit/$dir/build/pluginlist-entry.xml" --version "$VERSION" --id "$id" --repo sd88me/mpc-vst-TR-MPC --license GPL-3.0-only \
    --requires "TR-MPC (same version) in the same project" \
    --about "$name: puts TR-MPC slots and its reverb/delay sends on their own MPC track ($kind)" -o "$DIST"
  ZIPS="$ZIPS $(ls "$DIST"/"${name// /-}"-"$VERSION"-*.zip)"
done
ls -l $ZIPS
if [ -n "$DEVICE" ]; then
  for Z in $ZIPS; do
    d=$(basename "$Z" -mpc-armv7.zip)
    ssh "root@$DEVICE" 'cat > /tmp/trmpc-release.zip' < "$Z"
    ssh "root@$DEVICE" "cd /tmp && rm -rf '$d' && unzip -o -q trmpc-release.zip && cd '$d' && sha256sum -c SHA256SUMS >/dev/null && sh install.sh -y; cd /tmp && rm -rf '$d' trmpc-release.zip"
  done
  # Each MPC start leaves ~100-200 MB of resampled knob filmstrips (/var/tmp/filmstrips/temp_*.img, on the 5.6 GB /data partition) that
  # a killed MPC never deletes; after a few dozen restarts /data is full and installs fail writing MPC.settings. Remove the ones that
  # predate the running MPC (its start time from /proc, parsed after the ")" of its comm, which contains spaces).
  sleep 20
  ssh "root@$DEVICE" 'P=$(pidof MPC | cut -d" " -f1); [ -n "$P" ] || exit 0
    E=$(( $(awk "/^btime/{print \$2}" /proc/stat) + $(sed "s/.*) //" /proc/$P/stat | awk "{print \$20}") / 100 ))
    touch -d "$(date -d @$E "+%Y-%m-%d %H:%M:%S")" /tmp/mpcstart
    D=/data/system/var/overlay/tmp/filmstrips; n=$(find $D -name "temp_*.img" ! -newer /tmp/mpcstart 2>/dev/null | wc -l)
    find $D -name "temp_*.img" ! -newer /tmp/mpcstart -exec rm -f {} + 2>/dev/null; rm -f /tmp/mpcstart
    echo "removed $n stale filmstrip files; /data: $(df -h /data | tail -1)"'
fi
