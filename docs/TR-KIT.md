# TR-MPC (modular kit), design notes (released as 1.0.0)

Sixteen slots; each slot picks any of 49 voices (6W6 8, 8W8 16, CW-78 14, 9W9 11) via `sNN_src`, with Level, Tune, Decay, Drive, Rev and Dly forwarded to that voice's own pots, plus a volume per kit. Pads: MIDI notes 36-51 -> slots 1-16.

How: `ports/trkit/src/trkit.cpp` links the four engines in one plugin. The engines are the vendored DSP plus a small per-voice tap (`ports/trkit/patches/<kit>.patch`, applied by `tools/apply_trkit_patches.sh` into the git-ignored `ports/trkit/engines/`; `ports/<kit>/src` stays byte-identical to upstream). With a tap set, an engine renders only its voices and hands each voice's post-fader dry sample to trkit, which applies the slot's pan and sends and runs ONE reverb, delay and master stage (8W8's own, `sc808_fx_stereo`) for the whole kit. 6W6 also gets an activity gate in tap mode (its metal lanes otherwise render when silent). Parameter list: `tools/gen_trkit_params.py` -> `ports/trkit/params.json` (361 params; append-only now that 1.0.0 is released).

Per slot: Voice (49 options), Distortion, Level, Tune, Decay, Drive and the voice's own extras (Attack, Tone, Snappy, Noise, Rate, Sweep and so on), then Pan, Rev, Dly (the slot's). Shared: Rev decay/tone/HPF/level, Dly time/feedback/tone/HPF/level, Master dist/drive, Comp, Volume, internal reverb/delay/comp on-off keys, plus the randomise module. After the shared chain an output soft limiter keeps summed pads from clipping. Host tempo reaches the synced delay.

Limits:
- Two slots on the same voice of the same kit share that voice (one engine voice, retriggered); they can differ in pan and sends.
- Only Level, Tune, Decay, Drive, Distortion and nine extra pots per slot are VST parameters; any other engine pot keeps its default and is not saved.
- An engine or the FX stage that has been silent for 5 s is not rendered until the next trigger.
- Bench (Force, `bench.txt`): p99 24.8%, worst block 27.4% (WARN: one instance per project). Before the shared chain it was 36.7%.

Released: TR-MPC 1.0.0 and TR-MPC Tap FX 1.0.0 (a companion effect that sends slots or the reverb/delay sends to their own MPC track). Still to do: kit presets, see `HANDOFF.md`.

Tests: `bash ports/trkit/test/run.sh` applies the patches and triggers all 49 voices through the plugin under ASan/UBSan (each must be audible and decay), then checks that Pan moves a slot, Rev adds a tail and the Tap FX routing (PASSED).
