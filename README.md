# ABYSS

Experimental noise synthesizer firmware for the M-VAVE FM-1, inspired by Death By Audio's APTBS Synthesizer.
Built on [felucca-bsp](https://github.com/hugelton/felucca-bsp) and Felucca's build tools.

**Status: Milestone 2** — three oscillators, white/pink noise, mixer, filter, envelope, keyboard and USB-MIDI
playback, drone mode, the minimal UI with performance mode. See `docs/ARCHITECTURE.md`.

![screens](docs/screens.png)

## Controls

| Control | Does |
|---|---|
| K1–K4, SELECT, ALGORITHM, PRESETS | the page's seven parameters, in that order (turning one makes it the big value) |
| MASTER | output level |
| HOME | MAIN — press again: SHAPE → LEVELS → MAIN |
| ENV / EDIT | envelope / filter page |
| LFO, FX, SAVE | MOD, FX, PRESETS pages (Milestones 3–5) |
| GLO | performance mode: the current value huge |
| PLAY | DRONE on/off (lit while on) |
| REC | TAP: held, plays the current note |
| OCT− / OCT+ | octave (both: reset) |
| Keys | mono, last note priority; USB-MIDI notes on any channel |

Pages: **MAIN** FREQUENCY, OSC 2, OSC 3, NOISE, MIX, FILTER, RESONANCE · **SHAPE** WAVE 1–3, PULSE WIDTH,
DETUNE, NOISE TYPE, SUB · **LEVELS** LEVEL 1–3, SUB, NOISE, MIX · **ENVELOPE** ATTACK, DECAY, SUSTAIN, RELEASE,
ENV > FILTER · **FILTER** FILTER, RESONANCE, ENV > FILTER.

## Try it without hardware

Open <https://joakimeriksson.github.io/fm1-emu/>, choose to open a local package and pick `abyss.fwsc`.

## Build (macOS, Apple silicon)

Needs Python 3 with Pillow (`pip3 install Pillow`), Docker Desktop (the JieLi compiler is Linux x86-64), the
JieLi toolchain and three files of the AC79 SDK (see `build_mac.sh`). Then:

```
./build_mac.sh            # -> build/abyss.fwsc
```

Host DSP test (any C compiler): `cc -O2 -Ibuild/gen -Ifirmware/dsp host/render.c -o build/render && build/render build/wav`

## Install / recovery

At your own risk; keep the official firmware at hand. Install with Felucca's command-line installer
(`python3 firmware/bsp/tools/fm1_install.py build/abyss.fwsc`, needs `pip3 install mido python-rtmidi`) or a local copy of
Felucca's web installer. ABYSS keeps the update entry, so any installer can replace it again (Felucca's web
installer also restores M-VAVE's V15/V16). If a build fails to boot twice in a row the FM-1 drops into UBOOT; from
there, recovery uses FM-1-transporter / `firmware/bsp/tools/fm1_rescue.sh`.

## Licence

GPL-3.0-only (it uses Felucca's build tools and update loader). felucca-bsp files keep MPL-2.0; Inter Tight is OFL.
