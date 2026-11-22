# Changelog

## 1.0.1
- The install handshake as current felucca-bsp answers it: M-VAVE's updater sees `FM-1_130`, Felucca's and the
  hub's installers the whole `FM-1_1300`, plus the flash chip's ID (backport of felucca-bsp's `ota.c` handshake;
  `tools/fm1_install.py` and the rescue tool updated to match).
- `fmhub.json` for The Firmware Hub; README: where ABYSS keeps its data.

## 1.0
- VOICES: MONO or 2–4 voices, with SPREAD and GLIDE (CC 5).
- MIDI clock sync for LFO 1, LFO 2, RANDOM and the delays; START restarts the LFOs.
- 4 new factory sounds: GHOST CHOIR, ACID TUNNEL, BROKEN CHORDS, FM BELLS.
- The boot log is written once per start; `docs/INSTALL.md`; the version on the splash.

## Milestone 6
- TRS MIDI IN; MIDI CCs, pitch bend, program change, sustain, velocity.
- ENV MOD and OUTPUT pages (stereo WIDTH, MIDI CH, BEND); the last sound and the MIDI settings kept over a
  power-off.
