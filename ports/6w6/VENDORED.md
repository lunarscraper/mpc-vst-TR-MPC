# Vendored: 6W6
Upstream: https://github.com/athousanddetails/schwung-6W6 at 2bda07e4261a118f402c03950bd2a8ca78f89b61 (GPL-3.0). Copied unmodified: src/, scripts/, LICENSE, THIRD_PARTY.md. Local changes: STEMS, below.

## Local change: STEMS (this fork, 1.2.0)

- `src/dsp/sd606_engine.cpp` / `.h`: `sd606_set_stem()`; with a buffer set, render also writes each voice's post-fader
  sample, the reverb and delay returns, the master compressor's gain and VOLUME there. The output is unchanged, bit for
  bit (checked against the unmodified engine). The wet returns and the glue are computed into named values first; the
  arithmetic and its order are the same.
- `src/dsp/sd606_plugin.cpp`: `trstem_*()`, the kit's name and voices for `src/mpc_pads_engine.c`, which records them.
