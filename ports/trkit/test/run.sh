#!/usr/bin/env bash
# Offline audio test for TR-MPC. Run from a path without spaces:   bash ports/trkit/test/run.sh
set -euo pipefail
P="$(cd "$(dirname "$0")/../.." && pwd)"   # ports/
bash "$P/../tools/apply_trkit_patches.sh"
O=/tmp/trkit_audio; rm -rf $O; mkdir -p $O
CF="-I$P/trkit/src -I$P/trkit/engines/6w6 -I$P/6w6/src/vendor/606 -I$P/trkit/engines/8w8 -I$P/trkit/engines/cw78 -I$P/trkit/engines/9w9 -O2 -g -fsanitize=address,undefined"
g++ -std=gnu++14 $CF -c $P/trkit/src/trkit.cpp -o $O/trkit.o
g++ -std=gnu++14 $CF -c $P/trkit/engines/6w6/sd606_engine.cpp -o $O/a.o
g++ -std=gnu++14 $CF -c $P/trkit/engines/8w8/sc808_engine.cpp -o $O/b.o
g++ -std=gnu++14 $CF -c $P/trkit/engines/cw78/cr78_engine.cpp -o $O/c.o
gcc -std=gnu11 $CF -c $P/trkit/engines/9w9/er99_engine.c -o $O/d.o
g++ -std=gnu++14 $CF -c $P/trkit/test/audio_test.cpp -o $O/t.o
g++ -fsanitize=address,undefined $O/*.o -lm -o $O/audio_test
TRKIT_DIR="$P/9w9/src" $O/audio_test
