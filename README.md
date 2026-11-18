# ABYSS

Experimental noise synthesizer firmware for the M-VAVE FM-1, inspired by Death By Audio's APTBS Synthesizer.
Built on [felucca-bsp](https://github.com/hugelton/felucca-bsp) and Felucca's build tools.

**Status: Milestone 4** (drive, filter types, dual feedback delay, stereo) on top of Milestone 3: — three oscillators with FM, cross mod, ring mod, hard sync and CHAOS; six noise types;
mixer, filter, envelope; two LFOs and a random sample-and-hold with routing; keyboard and USB-MIDI playback, drone
mode, the minimal UI with performance mode. See `docs/ARCHITECTURE.md`.

![screens](docs/screens.png)

## Controls

Every page is one topic with four parameters on **K1–K4** (shown two by two: K1 K2 above, K3 K4 below). The left
knobs do the same job on every page.

| Control | Does |
|---|---|
| K1–K4 | the page's four parameters (turning one makes it the big value) |
| SELECT | turn through the pages |
| ALGORITHM | filter cutoff, on every page |
| PRESETS | turn through the sounds: 10 factory presets, then your saved ones (each loads as you land on it) |
| MASTER | output level |
| HOME | next page, round all fourteen |
| EDIT / ENV | FILTER / ENVELOPE |
| LFO | LFO 1 → LFO 2 → RANDOM |
| SAVE | PRESETS page: K1 picks USER 1–10, press SAVE again to store the current sound |
| FX | DRIVE → DELAY 1 → DELAY 2 → DELAYS |
| GLO | performance mode: the current value huge |
| PLAY | DRONE on/off (lit while on) |
| REC | TAP: held, plays the current note |
| OCT− / OCT+ | octave (both: reset) |
| Keys | mono, last note priority; USB-MIDI notes on any channel |

| # | Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| 1 | OSC 1 | FREQUENCY | WAVE | LEVEL | SUB OSC |
| 2 | OSC 2 | TUNE | WAVE | LEVEL | DETUNE 2/3 |
| 3 | OSC 3 | TUNE | WAVE | LEVEL | PULSE WIDTH |
| 4 | NOISE | LEVEL | TYPE | CHAOS | – |
| 5 | CROSS | FM 2 > 1 | X-MOD 1 <> 3 | RING 1 x 2 | SYNC |
| 6 | FILTER | CUTOFF | RESONANCE | ENV AMOUNT | TYPE (LP/BP/HP) |
| 7 | ENVELOPE | ATTACK | DECAY | SUSTAIN | RELEASE |
| 8 | DRIVE | TYPE | DRIVE | TONE | MIX |
| 9 | DELAY 1 | TIME | FEEDBACK | WOBBLE | PAN |
| 10 | DELAY 2 | TIME | FEEDBACK | WOBBLE | PAN |
| 11 | DELAYS | WET/DRY | X-FEEDBACK | TONE | ROUTING |
| 12 | LFO 1 | RATE | DEPTH | WAVE | TARGET |
| 13 | LFO 2 | RATE | DEPTH | WAVE | TARGET |
| 14 | RANDOM | RATE | DEPTH | SMOOTH | TARGET |

## Presets

Factory: 1 INDUSTRIAL DRONE · 2 METALLIC MACHINE · 3 BROKEN OSC · 4 FEEDBACK STORM · 5 DARK AMBIENT ·
6 DIGITAL COLLAPSE · 7 NOISE BURST · 8 RADIO INT · 9 SUBHARMONIC · 10 CHAOTIC MACHINE.
Power-on plays preset 1. A sound changed since it loaded shows EDITED. User slots live in flash (two alternating
4 KiB sectors at 0xF0000, so a power cut during a save loses only that save); a save silences the audio for
~20 ms while the sector erases. The factory sounds use the drive and delays.

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
