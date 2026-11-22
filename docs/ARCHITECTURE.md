# ABYSS — architecture (v1.0)

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

## Milestone 4: effects

`voice → FILTER (LP / BP / HP) → VCA → DRIVE → DELAY 1, DELAY 2 → stereo → soft clip → MASTER`

* **DRIVE**: SOFT (cubic), HARD, FOLD (triangle wavefolder), FUZZ (asymmetric, LM386-ish), CRUSH (2–16 bits),
  DECIMATE (sample hold 1–51); TONE (one-pole, 400 Hz–18 kHz) and MIX. The clippers' output is trimmed as DRIVE rises.
* **DELAYS**: two lines of 32 768 int16 samples stored at FS/2 (1.48 s each, the top above 11 kHz gone, as a PT2399 —
  128 KiB of the pool). TIME 20 ms–1.4 s; the read position glides (~50 ms) so moving TIME bends the pitch.
  FEEDBACK to 110 %, WOBBLE (slow sine + flutter on the time), PAN. ROUTING SERIES (1 into 2, the APTBS way) or
  PARALLEL; CROSS FEEDBACK sends each line into the other. The feedback path is low-passed (TONE), DC-blocked and
  soft-clipped as it is written: above 100 % it self-oscillates into a bounded roar and decays to silence once
  the feedback comes down (host test 13). WET/DRY balances voice and echoes.
* New modulation targets: DRIVE, DELAY TIME (both, ±50 %), FEEDBACK (both, ±50 %).

## Milestone 6: MIDI, width, memory

* **TRS MIDI IN**: Felucca's UART receiver (`src/midi_uart.c`, `hal/fm1_uart.h`), polled from the tick every
  0.2 ms, feeding the same queue as USB-MIDI. `ui.c midi_poll` filters by MIDI CH and handles notes (velocity 0 =
  off), pitch bend (± BEND st, added to the pitch base), program change, sustain (deferred note-offs) and the CC map
  in the README. A CC sets a parameter scaled to its range and marks the sound EDITED.
* **Velocity**: a gain on the envelope, `1 − VELOCITY·(127 − vel)/127`; keys play at 127.
* **ENV MOD**: the envelope (computed before the modulation sum) adds AMOUNT × env to any destination.
* **WIDTH**: mid/side after the delays, side × WIDTH/50; above 50 % a 441-sample (10 ms) delayed copy of the dry
  signal is added to L and subtracted from R.
* **Settings memory** (`presets.c set_*`): 8-byte records `{0x5A, entry, MIDI CH, BEND, xor}` appended to the
  sector at 0xF3000, the last valid one used at boot; erased only when full or holding someone else's data.
  MIDI CH and BEND are settings, not part of a sound: loading a preset leaves them alone.
* **Boot guard** (`abyss.c`): a power-on (P3 reset source bit 0) clears the failure count; a start that crashed or
  hit the 4 s watchdog before the main loop counts as failed, three in a row → the updater. A boot log at 0xF2000
  is shown by holding REC at power-on.

## v1.0: voices, clock, boot log

* **Voices** (`abyss_dsp.c`): `ab_voice_t` holds what each voice owns — oscillator phases and last outputs, SVF
  state and coefficient, envelope, VCA level, glided pitch. A block runs: the modulators; every voice's envelope;
  the shared noise for the block (`ab_white`, `ab_nz`); each sounding voice's control values (`ab_vblk_t`) and
  its samples added into `ab_sum` (`ab_voice_run`; an idle voice is skipped); then LEVEL, DRIVE, the delays and
  WIDTH once on the sum, scaled by 1/√voices. MONO renders bit-identically to Milestone 6 (host test).
  ENV MOD moves pitch, OSC 2, cutoff, resonance and FM per voice; its other targets follow the voice started
  last (`ab.vlead`). The UI (`ui.c poly_on/poly_off`) gives a note the voice already on it, else the one let go
  longest ago, else the oldest; changing VOICES (or loading a sound with a different count) lets every voice go.
* **MIDI clock** (`ui.c midi_poll`): each F8 is timestamped by the MIDI queue; the tempo is the time across the
  last 24 ticks (`ab.spq`, samples a quarter note, smoothed while steady), dropped after 0.6 s without ticks.
  With CLOCK on (a setting, kept with MIDI CH and BEND) `ab_rate_inc` turns an LFO / RANDOM rate into a note
  division and the delay times follow `AB_DDIV_U`. FA (START) restarts the LFO and RANDOM phases.
* **Boot log**: the bytes of a start are gathered in `.noinit` RAM (`blog_ram`) and written in one flash write at
  the 2 s healthy point, a fault, or the drop to UBOOT; a start the watchdog ends early leaves them in RAM and
  the next start writes them first. (Was one flash write per byte, ~16 a start.)

## Measurements (fm1-emu, 240 MHz model)

* v1.0, GHOST CHOIR with 4 voices held, both delays, clock sync, the UI busy: **~29 % CPU**, 856 µs worst
  half-buffer of 2 900, 0 late halves. RAM 19 KiB variables; image 129 KiB.

* Milestone 6, FEEDBACK STORM over TRS MIDI with CCs and bend, WIDTH 100 %: ~12 % CPU, 376 µs worst half-buffer,
  0 late halves.
* Milestone 4, FEEDBACK STORM preset playing with drive and both delays, the UI busy: **~12 % CPU**, 358 µs worst
  half-buffer of 2 900, 0 late halves. RAM: 15 KiB variables, 241 KiB pool (frame buffer + delay lines) of 360.

* Milestone 3, everything on (FM, cross mod, ring, sync, chaos, three modulators): **~6.5 % CPU**, 191 µs worst
  half-buffer, 0 late halves.

* Audio render: **~4 % CPU** average, 123 µs worst half-buffer of 2 900 µs; 0 late halves.
* Output continuous through page changes, perf mode and drone (no zero runs in the recording).
* Host test (`host/render.c`): no overflow at all parameters at maximum; peaks −2 dBFS with MASTER at 80 %.

The emulator is not cycle-exact; CPU figures must be re-measured on hardware (`cpu%=` on the USB serial console).

## Known limits / not yet done

* No MIDI out (no clock out either); voices share one filter type and one set of envelope times.
* Button roles follow Felucca's measured panel map; if a label is off on your unit, tell me.
