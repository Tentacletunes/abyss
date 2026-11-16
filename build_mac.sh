#!/bin/sh
# Build ABYSS on macOS (Docker Desktop running). The JieLi toolchain and the three SDK files come from the
# buildtools folder next to this one (BUILDTOOLS= to point elsewhere).   ./build_mac.sh  ->  build/abyss.fwsc
set -e
cd "$(dirname "$0")"
BT="${BUILDTOOLS:-../buildtools}"
if [ ! -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ]; then
    mkdir -p "$HOME/.jieli"
    tar -xJf "$BT/jieli-linux-toolchain.tar.xz" -C "$HOME/.jieli"
    ln -sfn "$(basename "$(ls -d "$HOME"/.jieli/jieli-linux-toolchains-* | sort | tail -1)")" "$HOME/.jieli/toolchain"
fi
SDK="$PWD/build/sdk"
mkdir -p "$SDK/cpu/wl82/tools/cfg"
cp "$BT/sdk/uboot.boot" "$BT/sdk/cfg_tool.bin" "$SDK/cpu/wl82/tools/"
cp "$BT/sdk/cfg/eq_cfg_hw.bin" "$SDK/cpu/wl82/tools/cfg/"
docker info >/dev/null 2>&1 || { echo "build_mac.sh: start Docker Desktop first"; exit 1; }
JIELI_TOOLCHAIN="$HOME/.jieli/toolchain" AC79_SDK="$SDK" python3 tools/build.py
