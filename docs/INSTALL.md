# Installing ABYSS on the FM-1

ABYSS replaces the FM-1's firmware. It keeps the update entry, so you can always install something else over it,
including M-VAVE's own firmware. At your own risk; keep a backup (step 4 makes one).

## 1. Once: the installer's Python

On a Mac, in Terminal:

```
python3 -m venv ~/fm1-env
~/fm1-env/bin/pip install mido python-rtmidi
```

## 2. Install or update

1. Connect the FM-1 with a USB **data** cable (a charge-only cable is not seen) and switch it on.
2. Quit any app that uses MIDI (DAW, editor, browser tab with Web MIDI): the installer needs the port.
3. Run, from the folder that holds `abyss/` and `abyss.fwsc`:

```
~/fm1-env/bin/python abyss/firmware/bsp/tools/fm1_install.py abyss.fwsc
```

The FM-1 restarts into ABYSS. The splash shows the version and build under the name, e.g.
`v1.0  2026-11-22 1a2b3c4`: compare it with the build you meant to install.

`… fm1_install.py --info` shows what is running (ABYSS reports `FM-1_1300`).

Your user sounds (USER 1–10), the last sound chosen and the MIDI settings stay through updates.

## 3. If something goes wrong

| What you see | What to do |
|---|---|
| `error: FM-1 not found` | Another app has the MIDI port, or the cable is charge-only. Quit MIDI apps, try another cable. |
| A black screen after switching on, every time | ABYSS stops itself after three starts in a row that crash before the main screen and waits in update mode (black). Install again (step 2), or run the rescue below. |
| Stuck in update mode, the installer can't reach it | Run the rescue (below). |
| It starts, but behaves oddly | Hold **REC** while switching on: the boot log (below). Send a photo. |

**Update mode by hand:** switch off, hold **OCT−** and **OCT+** together, switch on, and keep them held through
the 3 s countdown until the screen goes black. Any installer or the rescue tool can reach it then. Let go during
the countdown and ABYSS starts as usual.

**Rescue** (Felucca's tool; it backs up the whole flash to `~/fm1-rescue/` first; when it waits for update mode,
use the OCT− + OCT+ countdown above):

```
bash -c "$(curl -fsSL https://raw.githubusercontent.com/hugelton/felucca-bsp/main/tools/fm1_rescue.sh)"
```

Answer **no** when it offers to write: it only makes the backup and restarts the FM-1 into ABYSS. Then install
again with step 2 if needed.

## 4. Going back to the official firmware

Use Felucca's web installer (it restores M-VAVE's V15/V16) or M-VAVE's own updater. ABYSS's flash areas
(0xF0000–0xF3FFF: user sounds, boot log, settings) are not used by M-VAVE's firmware in a way that matters: if ABYSS finds
something else there later, it starts those areas afresh.

## 5. The boot log

Hold **REC** while switching on: instead of the sound, the last bytes of the start log, as hex, until any button.
Each start is one line of codes:

| Code | Means |
|---|---|
| `B0 pp ss` | a start; `pp` the reset cause: `01` power switch, `40` restart after an install, `04` bit set: the watchdog |
| `14 60 85` | the flash chip's id |
| one byte | failed starts in a row before this one (3 → update mode, logged `A9`) |
| `B1 B2` | power and memory guards set up |
| `C0`–`C5` | screen, flash, sound and presets, keys, audio, USB |
| `A8` | update mode chosen by hand (OCT− + OCT+) |
| `C8` | the main loop runs (a good start) |
| `C9` | still running 2 s later |
| `EE vv pppppppp …` | a crash: vector and address (send a photo) |

The log is written once per start (at `C9`, or at a crash), so it costs the flash almost nothing.

## 6. Building it yourself

See the README: `./build_mac.sh` (Docker Desktop and the JieLi toolchain in `buildtools/`) makes `build/abyss.fwsc`.
