# Vendored: schwung-cw-78
Upstream: https://github.com/athousanddetails/schwung-cw-78 at e4b529e7c633cf7f4a01ad817b5db488a0b74cca (GPL-3.0). Copied unmodified. Local changes: STEMS, below.

## Local change: STEMS (this fork, 1.2.0)

- `src/dsp/cr78_engine.cpp` / `.h`: `cr78_set_stem()`; with a buffer set, render also writes each voice's post-fader
  sample, the reverb and delay returns, the master compressor's gain and VOLUME there. The output is unchanged, bit for
  bit (checked against the unmodified engine). The wet returns and the glue are computed into named values first; the
  arithmetic and its order are the same.
- `src/dsp/cr78_plugin.cpp`: `trstem_*()`, the kit's name and voices for `src/mpc_pads_engine.c`, which records them.
