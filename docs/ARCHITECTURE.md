# ABYSS — architecture (Milestones 1–3)

## Hardware, as verified from source

| | Fact | Source |
|---|---|---|
| SoC | JieLi AC79 (WL82), `pi32v2` core, 1 core used (a 2nd exists, beta in the BSP) | felucca-bsp `docs/CPU.md`, `chip/fm1_cpu1.h` |
| Clock | 360 MHz as the boot loader leaves it (the emulator models 240 MHz) | `docs/CPU.md`, fm1-emu |
| FPU | **none** — `float` compiles to library calls. Hardware 32-bit divide exists | disassembly of a test file with the JieLi clang 4.0.1 |
| CPU erratum | a 64-bit multiply (`rN_rM = rA * rB`) next to 16-bit loads can corrupt a register. ABYSS uses **no 64-bit multiplies**; `tools/build.py` lists any function that has both | `docs/CPU.md` |
| Audio | ALNK0 I2S → external codec, stereo, 24-bit words, **44 117.6 Hz** (24 MHz / 544), double-buffered DMA, an interrupt per half | `boards/fm1/fm1_audio.h` |
| RAM | 72 KiB for variables + 360 KiB "pool" for big buffers (same SRAM). ABYSS uses 10 KiB + 113 KiB (frame buffer) | `app.ld`, build output |
| Flash | 1 MiB (Puya P25Q80 or UC25HQ80 on newer units); app slot 568 KiB. ABYSS is 109 KiB | `tools/fm1pkg_make.py` |
| Display | ST7789 240×240 RGB565 on SPI1 at 12 MHz, DMA. A full frame takes ~77 ms | `boards/fm1/fm1_lcd_hw.h` |
| Controls | 7 encoders, MASTER pot (ADC), 14 buttons, 27 keys (F3–G5), 41 LEDs — one scanned matrix, 10 kHz TIMER5 interrupt | `boards/fm1/fm1_input.h` |
| MIDI | USB-MIDI (in/out) in `chip/usb.c`; TRS MIDI IN via UART (not wired in ABYSS yet) | `chip/usb.c`, `chip/fm1_uart.h` |
| Update | USB-MIDI "M-UPGRADE" session (`chip/ota.c`) + the update loader; boot-loop guard drops to UBOOT | `chip/ota.c`, `firmware/loader` |

## What was reused from Felucca, what was replaced

Felucca = felucca-bsp (hardware layer, MPL-2.0) + Felucca-Engines (MIT) + Felucca's own app (GPL-3.0: 4-part
groovebox UI, sequencer, voice allocator, FX, presets). ABYSS keeps the first and the build/packaging tools, and
replaces the app:

| Kept unchanged | Replaced by ABYSS |
|---|---|
| `firmware/bsp/` — boot, interrupts, guards, flash driver, LCD/audio/input/ADC drivers, USB (MIDI, serial, update) | `firmware/src/abyss.c` — boot, audio ISR, 10 kHz tick, update hooks, main loop |
| `firmware/loader/` — the update loader | `firmware/dsp/` — the ABYSS engine |
| `tools/fm1pkg_make.py`, `tools/lz4blk.py` — packaging | `firmware/src/gfx.c`, `ui.c` — frame buffer, fonts, UI |
| Inter Tight font (OFL) | `tools/build.py`, `gen_fonts.py`, `gen_tables.py` |

## Runtime structure

```
TIMER5 10 kHz (prio 4) ── key/encoder scan, ms clock, USB poll (2 kHz)
ALNK0 half-buffer (prio 3) ── 4 × dsp_render(32 frames) ── ×128 → 24-bit ── DMA
main loop ── ui_poll (keys → notes, buttons, encoders, USB-MIDI) ── MASTER pot ── ui_frame (≤30 fps)
             └── while the LCD DMA sends a 16-row chunk, ui_poll runs again (note latency stays ~5 ms)
```

The UI is the only writer of the parameters (`ab.p[]`, int16, atomic); the DSP reads them once per 32-sample
block. Notes go through `ab.note / ab.gate / ab.trig`.

## Sound engine (Milestone 2)

`OSC1..3 + SUB + NOISE → mixer → 2× oversampled saturating SVF low-pass → VCA (ADSR, or DRONE) → soft clip → MASTER`

* Oscillators: sine (table), triangle, PolyBLEP saw, PolyBLEP square with pulse width; free-running phases.
  OSC 2/3 are tuned in semitones against OSC 1, ±DETUNE cents. SUB is a square an octave below OSC 1.
* Noise: xorshift white; pink via Kellet's 3-pole filter.
* Q15 samples in int32; every product is kept below 2³¹ by clamping (host test checks overflow).
* APTBS mapping so far: the diode-clipped filter → saturating SVF state; the tap pad → REC; drone mode → PLAY.
  Still to come: diode-synced VCO2/3 and VCO1 cross-mod (M3), LM386-style drive + folds (M4), the two
  PT2399 delays in series with feedback and blend (M4).

## Milestone 3: experimental synthesis

* **FM 2 > 1**: linear, through-zero FM of OSC 1 by OSC 2 (100 % = index 2). **CROSS MOD 1 <> 3**: OSC 3 bends
  OSC 1 and OSC 1 bends OSC 3, each from the other's last sample — a feedback pair, the digital cousin of the
  APTBS VCO interaction. **RING 1 x 2** adds the product. **SYNC** restarts OSC 2, 3 or both when OSC 1 wraps
  (the APTBS diode sync), at the right fractional phase.
* **CHAOS**: each oscillator drifts on its own slow random walk (up to ±12 semitones) and gets white-noise FM.
* **Noise**: WHITE, PINK, BROWN, DIGITAL (15-bit LFSR clocked at 16× OSC 1), METAL (its 93-step short mode), CRACKLE.
* **Modulation** (block rate, 1.4 kHz): LFO 1, LFO 2 (sine, triangle, square, saw, random) and RANDOM (sample and
  hold with SMOOTH), each with a depth and one destination: pitch, OSC 2 pitch, filter, resonance, level, FM, noise.
* FM math stays in 32 bits: the increment is bent as `(inc >> 18) * m * 8`, so frequencies above ~11 kHz are not
  bent further; outputs are clamped before they modulate. The host test now runs under UBSan with a 60-second
  random-parameter fuzz: no overflow, no undefined behaviour.

## Measurements (fm1-emu, 240 MHz model)

* Milestone 3, everything on (FM, cross mod, ring, sync, chaos, three modulators): **~6.5 % CPU**, 191 µs worst
  half-buffer, 0 late halves.

* Audio render: **~4 % CPU** average, 123 µs worst half-buffer of 2 900 µs; 0 late halves.
* Output continuous through page changes, perf mode and drone (no zero runs in the recording).
* Host test (`host/render.c`): no overflow at all parameters at maximum; peaks −2 dBFS with MASTER at 80 %.

The emulator is not cycle-exact; CPU figures must be re-measured on hardware (`cpu%=` on the USB serial console).

## Known limits / not yet done

* No TRS MIDI IN yet, no velocity, no preset storage (Milestone 5 will use the flash sectors Felucca uses).
* Stereo: the path is stereo but L = R until the stereo delays (Milestone 4).
* Button roles follow Felucca's measured panel map; if a label is off on your unit, tell me.
