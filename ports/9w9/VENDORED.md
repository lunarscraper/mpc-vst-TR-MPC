# Vendored: schwung-9W9
Upstream: https://github.com/athousanddetails/schwung-9W9 at 10cbe5c1e1c9e65b0818bb34644ab1a13092082c (GPL-3.0). Copied unmodified. Local changes: STEMS, below.

## Local change: STEMS (this fork, 1.2.0)

- `src/dsp/er99_engine.c` / `.h`: `er99_engine_set_stem()`; with a buffer set, render also writes each voice's post-fader
  sample, the reverb and delay returns, the master compressor's gain and VOLUME there. The output is unchanged, bit for
  bit (checked against the unmodified engine). The wet returns and the glue are computed into named values first; the
  arithmetic and its order are the same.
- `src/dsp/er99_plugin.c`: `trstem_*()`, the kit's name and voices for `src/mpc_pads_engine.c`, which records them.
