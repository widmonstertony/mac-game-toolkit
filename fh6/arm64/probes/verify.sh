#!/bin/bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
highball="${HIGHBALL_HOME:-$HOME/Library/Application Support/Highball}"
protonish="${PROTONISH_HOME:-$HOME/Library/Application Support/protonish}"
engine="$highball/engines/arm64-kk-wine11.18-r4"
work=$(mktemp -d "${TMPDIR:-/tmp}/fh6-arm64-probe.XXXXXXXX")
printf 'Probe files and logs: %s\n' "$work"
export DYLD_FALLBACK_LIBRARY_PATH="$engine/engine/lib:$engine/frameworks"

case "${1:-}" in
    metal)
        : "${VULKAN_INCLUDE:?Set VULKAN_INCLUDE to the Vulkan-Headers include directory}"
        export FSR4_ROOT="${FSR4_ROOT:-$protonish/fsr4-metalfx-v3}"
        export FSR4_BUILD_DIR="$work/fsr-build" FSR4_SETTINGS=none FSR4_PROFILE=int8-lite
        mkdir "$FSR4_BUILD_DIR"
        cp -cR "$FSR4_ROOT/build/." "$FSR4_BUILD_DIR"
        common=(-O2 -fobjc-arc -arch arm64 -I "$FSR4_ROOT/src")
        xcrun clang "${common[@]}" -c "$FSR4_ROOT/hook/fsr4_metalfx.m" -o "$work/hook.o"
        for source in fsr4_live fsr411_live fsr412_live fsr4_panel; do
            xcrun clang++ "${common[@]}" -std=c++17 -c "$FSR4_ROOT/src/$source.mm" -o "$work/$source.o"
        done
        xcrun clang++ -dynamiclib -arch arm64 "$work/"*.o -framework Foundation -framework Metal \
            -framework MetalFX -o "$work/libfsr4-arm64.dylib"
        xcrun clang++ "${common[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
            -I "$VULKAN_INCLUDE" "$here/metal-roundtrip.mm" -framework Foundation -framework Metal -o "$work/metal"
        "$work/metal" "$engine/renderers/kosmickrisp/libvulkan_kosmickrisp.dylib" \
            "$work/libfsr4-arm64.dylib" 2>&1 | tee "$work/metal.log"
        ;;
    steam)
        : "${LLVM_MINGW_BIN:?Set LLVM_MINGW_BIN to the llvm-mingw toolchain bin directory}"
        bridge="$protonish/bridge"
        overlay="$work/overlay"
        mkdir -p "$overlay/aarch64-windows" "$overlay/aarch64-unix"
        cp "$bridge/x86_64-windows/lsteamclient.dll" "$overlay/aarch64-windows/"
        cp "$bridge/aarch64-unix/lsteamclient.so" "$overlay/aarch64-unix/"
        ln -s "$engine/engine/lib/wine/aarch64-unix/ntdll.so" "$overlay/aarch64-unix/ntdll.so"
        compiler="$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang"
        "$compiler" -shared -Wall -Wextra -Werror "$here/steam-module.c" "$here/steamclient64.def" \
            -o "$work/steamclient64.dll"
        "$compiler" -Wall -Wextra -Werror "$here/steam-ipc.c" -o "$work/steam-ipc.exe"
        export WINEPREFIX="$work/prefix" WINEDLLPATH_PREPEND="$overlay" WINEDEBUG=fixme-all
        export WINEDLLOVERRIDES='winemenubuilder,mscoree,mshtml,winedbg=d;lsteamclient=b;steamclient64=n'
        export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS}"
        trap '"$engine/engine/bin/wineserver" -k >/dev/null 2>&1 || true' EXIT
        cd "$work"
        "$engine/engine/bin/wine" "$work/steam-ipc.exe" 2>&1 | tee "$work/steam.log"
        ;;
    *)
        printf 'Usage: %s metal|steam\n' "$0" >&2
        exit 2
        ;;
esac
