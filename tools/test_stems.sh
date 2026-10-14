#!/usr/bin/env bash
# STEMS, offline (x86, ASan/UBSan): builds tools/stems_test.c against one kit's engine and ports/common's adapter
# (no VST wrapper), records a take into ports/<kit>/build/stems_test/ and checks it. From the repo root:
#   bash tools/test_stems.sh 8w8
set -euo pipefail
cd "$(dirname "$0")/.."
K="$1"
P="ports/$K"
case "$K" in
  6w6) SRC="$P/src/dsp/sd606_plugin.cpp $P/src/dsp/sd606_engine.cpp"; SEND=sd_rev; COMP=comp; INC="-I$P/src/vendor/606" ;;
  8w8) SRC="$P/src/dsp/sc808_plugin.cpp $P/src/dsp/sc808_engine.cpp"; SEND=sd_rev; COMP=comp; INC="" ;;
  cw78) SRC="$P/src/dsp/cr78_plugin.cpp $P/src/dsp/cr78_engine.cpp"; SEND=sd_rev; COMP=comp; INC="" ;;
  9w9) SRC="$P/src/dsp/er99_plugin.c $P/src/dsp/er99_engine.c"; SEND=sd_c_rev; COMP=master_comp; INC="" ;;
  *) echo "kit: 6w6 8w8 cw78 9w9"; exit 2 ;;
esac
B="$P/build/stems_test.d"
rm -rf "$B" && mkdir -p "$B"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
FL="-I$P/src -I$P/src/dsp -I$P/src/host $INC"
OBJS=""
for f in $SRC; do
  o="$B/$(basename "$f").o"
  case "$f" in
    *.cpp) g++ $SAN -std=gnu++14 $FL -c "$f" -o "$o" ;;
    *) gcc $SAN -std=gnu11 $FL -c "$f" -o "$o" ;;
  esac
  OBJS="$OBJS $o"
done
gcc $SAN -std=gnu11 $FL -c "$P/src/mpc_pads_engine.c" -o "$B/adapter.o"
gcc $SAN -std=gnu11 -I"$P/src" -c tools/stems_test.c -o "$B/stems_test.o"
g++ $SAN $OBJS "$B/adapter.o" "$B/stems_test.o" -lm -lpthread -o "$B/stems_test"
"$B/stems_test" "$P/build/stems_test" "$P/src" "$SEND" "$COMP"
