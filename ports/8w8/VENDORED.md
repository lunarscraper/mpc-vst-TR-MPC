# Vendored: schwung-8W8
Upstream: https://github.com/athousanddetails/schwung-8W8 at 94aa2716d4c2e5851fc7c5dbb4c17adfd133c997 (GPL-3.0). Copied unmodified, except for the local changes below.

## Local changes

**Punch kick** (a second bass drum model, not part of schwung-8W8):
- `src/dsp/sc808_bd_punch.h` (new): one phase-reset oscillator under a pitch envelope, with harmonics, pulse, noise, gate, envelope compression and a one-knob EQ.
- `src/dsp/sc808_engine.cpp`: `bd_model` picks the voice at trigger time; both models feed the one kick lane. With `bd_model` at its default (808) the output is bit-identical to upstream.
- `scripts/gen_params.py`: eight parameters (`bd_pitch`, `bd_pdecay`, `bd_harm`, `bd_pulse`, `bd_noise`, `bd_gate`, `bd_comp`, `bd_model`) registered after everything that existed, so no state-blob slot and no VST parameter index moves. `src/dsp/sc808_params.h` and `module.mpc.json` are regenerated from it.
- `src/tools/loadtest.c`: a "Punch kick" section.
- `layout.conf`: regenerated with `tools/make_layout.py` (a PUNCH KICK panel next to BASS DRUM; the other panels move up one place).

The Move-side files (`ui_chain.js`, `web_ui.html`, `help.json`) are not updated for the new page.

