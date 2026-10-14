#!/usr/bin/env bash
# Build ports/trkit/engines/<kit>/ = a copy of each vendored engine (ports/<kit>/src/dsp) plus ports/trkit/patches/<kit>.patch
# (the per-voice tap TR-MPC needs). The vendored sources are never edited. Run before building trkit.
set -euo pipefail
cd "$(dirname "$0")/../ports"
rm -rf trkit/engines
for k in 6w6 8w8 cw78 9w9; do
  mkdir -p trkit/engines/$k
  cp -r $k/src/dsp/. trkit/engines/$k/
  patch -s -p1 -d trkit/engines/$k < trkit/patches/$k.patch
done
