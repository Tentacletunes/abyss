#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# ABYSS build, after Felucca's tools/build.py (Leo Kuroshita, Hügelton Instruments).
"""Build ABYSS for the M-VAVE FM-1.

    JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK python3 tools/build.py

Makes build/abyss.bin (the app), build/loader/ota.bin (the update loader, Felucca's) and build/abyss.fwsc (the
installable package). On anything but Linux x86-64 the JieLi tools run in a linux/amd64 Docker container.
"""
import os
import platform
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SRC / "tools"))
import lz4blk          # noqa: E402
import fm1pkg_make     # noqa: E402

FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
APP_XIP = 0x02000120
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"
PRODUCT = os.environ.get("ABYSS_ID", "FM-1_1300")
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
HAL_DIRS = (FW / "bsp" / "chip", FW / "bsp" / "boards" / "fm1")
HAL_INC = [f"-I{d.relative_to(SRC)}" for d in HAL_DIRS]


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN", str(Path.home() / ".jieli" / "toolchain"))
    if not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    GEN.mkdir(parents=True, exist_ok=True)
    for tool, out in (("gen_tables.py", "abyss_tables.h"), ("gen_fonts.py", "abyss_fonts.h")):
        subprocess.run([sys.executable, str(SRC / "tools" / tool), str(GEN / out)], check=True)


# ---- the update loader (Felucca's firmware/loader, GPL-3.0-only)
def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, *HAL_INC, "-Ifirmware/src"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    tc("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    return ota


# ---- the app
def ld_regions():
    regs = {}
    for m in re.finditer(r"^\s*(\w+)\s*\([rwx]+\)\s*:\s*ORIGIN\s*=\s*(\w+),\s*LENGTH\s*=\s*(\w+)",
                         (FW / "app.ld").read_text(), re.M):
        n = m.group(3)
        regs[m.group(1)] = (int(m.group(2), 0), int(n[:-1], 0) * 1024 if n.endswith("K") else int(n, 0))
    return regs


VERSION = "1.0"                                     # the release; the splash shows it with the commit


def build_tag():
    """what the splash shows: the version, the date and the source's git commit (+ if changed since), e.g.
    "v1.0  2026-11-18 13baf11" """
    import datetime
    try:
        h = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=SRC, capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=SRC, capture_output=True,
                               text=True).stdout.strip()
    except OSError:
        h, dirty = "", ""
    return f"v{VERSION}  {datetime.date.today().isoformat()} {h or 'nogit'}{'+' if dirty else ''}"


def build_app():
    flags = [*CFLAGS, *HAL_INC, "-Ifirmware/src", "-Ifirmware/dsp", "-Ibuild/gen", f'-DABYSS_ID="{PRODUCT}"',
             f'-DABYSS_BUILD="{build_tag()}"']
    bsp = FW / "bsp" / "chip"
    tc_all(("cc", "-c", bsp / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", bsp / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", bsp / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           ("cc", *flags, "-c", FW / "src" / "abyss.c", "-o", OUT / "abyss.o"))
    elf = OUT / "abyss.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
       OUT / "abyss.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                           ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                           ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                           ("common/bin/objdump", "-t", elf),
                           ("common/bin/objdump", "-d", elf))
    (OUT / "abyss.dis").write_text(dis)

    def symv(name):
        m = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        if not m:
            raise SystemExit(f"build: no symbol {name}")
        return int(m.group(1), 16)
    if symv("_start") != APP_XIP:
        raise SystemExit("build: _start is not at the XIP entry")
    img = bytearray((OUT / "text.bin").read_bytes())
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img)) + blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "abyss.bin").write_bytes(img)
    regs = ld_regions()
    ram = symv("_bss_end") - regs["RAM"][0]
    pool = symv("_pool_end") - regs["POOL"][0]
    print(f"  image {len(img)} B of {regs['XIP'][1]}; RAM {ram} B of {regs['RAM'][1]}; pool {pool} B of {regs['POOL'][1]}")
    if len(img) > regs["XIP"][1] or ram > regs["RAM"][1] or pool > regs["POOL"][1]:
        raise SystemExit("build: does not fit")
    # the AC79 can leave a register a bit off when a 64-bit multiply sits among 16-bit loads (felucca-bsp
    # docs/CPU.md): list the functions that have both, for review
    risky, fn, mul, h16 = [], None, False, False
    for ln in dis.splitlines():
        m = re.match(r"^([0-9a-f]+) <?(\w+)>?:", ln) or re.match(r"^(\w+):$", ln)
        if m:
            if fn and mul and h16:
                risky.append(fn)
            fn, mul, h16 = m.group(m.lastindex), False, False
            continue
        if re.search(r"r\d+_r\d+ = r\d+ \* r\d+", ln):
            mul = True
        if re.search(r"= h\[", ln):
            h16 = True
    if fn and mul and h16:
        risky.append(fn)
    if risky:
        print("  note: 64-bit multiply beside 16-bit loads in:", ", ".join(risky))
    return bytes(img)


def main():
    OUT.mkdir(exist_ok=True)
    generate()
    ota = build_loader()
    img = build_app()
    fm1pkg_make.SDK = os.environ.get("AC79_SDK", str(Path.home() / "fw-AC79_AIoT_SDK"))
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / "abyss.fwsc").write_bytes(pkg)
    print(f"app      build/abyss.bin  {len(img)} B")
    print(f"loader   build/loader/ota.bin  {len(ota)} B")
    print(f"package  build/abyss.fwsc  {len(pkg)} B, identity {PRODUCT}, build {build_tag()}")


if __name__ == "__main__":
    main()
