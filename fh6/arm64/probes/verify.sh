#!/bin/bash
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
highball="${HIGHBALL_HOME:-$HOME/Library/Application Support/Highball}"
protonish="${PROTONISH_HOME:-$HOME/Library/Application Support/protonish}"
engine="$highball/engines/arm64-kk-wine11.18-r4"
work=$(mktemp -d "${TMPDIR:-/tmp}/fh6-arm64-probe.XXXXXXXX")
printf 'Probe files and logs: %s\n' "$work"
export DYLD_FALLBACK_LIBRARY_PATH="$engine/engine/lib:$engine/frameworks"

build_native_fsr() {
    : "${VULKAN_INCLUDE:?Set VULKAN_INCLUDE to the Vulkan-Headers include directory}"
    : "${FFX_SDK_ROOT:?Set FFX_SDK_ROOT to the FidelityFX Kits/FidelityFX directory}"
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
}

case "${1:-}" in
    metal)
        build_native_fsr
        xcrun clang++ "${common[@]}" -std=c++17 -fdeclspec -Wall -Wextra -Werror -Wno-missing-field-initializers \
            -I "$VULKAN_INCLUDE" -isystem "$FFX_SDK_ROOT" "$here/metal-roundtrip.mm" \
            -framework Foundation -framework Metal -o "$work/metal"
        "$work/metal" "$engine/renderers/kosmickrisp/libvulkan_kosmickrisp.dylib" \
            "$work/libfsr4-arm64.dylib" 2>&1 | tee "$work/metal.log"
        ;;
    d3d12|live)
        : "${LLVM_MINGW_BIN:?Set LLVM_MINGW_BIN to the llvm-mingw toolchain bin directory}"
        : "${WINE_SOURCE:?Set WINE_SOURCE to the matching Highball Wine 11.18 source}"
        : "${WINE_BUILD:?Set WINE_BUILD to its configured build directory}"
        : "${VKD3D_INCLUDE:?Set VKD3D_INCLUDE to the VKD3D source include directory}"
        : "${VKD3D_GENERATED_INCLUDE:?Set VKD3D_GENERATED_INCLUDE to the directory containing the generated interop headers}"
        : "${FFX_FRAMEGEN_DLL:?Set FFX_FRAMEGEN_DLL to the Windows AMD frame generation DLL}"
        framegen_dir=$(cd "$(dirname "$FFX_FRAMEGEN_DLL")" && pwd)
        framegen_dll="$framegen_dir/$(basename "$FFX_FRAMEGEN_DLL")"
        test -f "$framegen_dll"
        build_native_fsr
        bridge="$here/../fsr4"
        xcrun clang -arch arm64 -c -D__WINESRC__ -DWINE_UNIX_LIB \
            -I "$WINE_BUILD/include" -I "$WINE_SOURCE/include" "$bridge/wine_heap.c" -o "$work/wine_heap.o"
        xcrun clang++ "${common[@]}" -std=c++17 -fdeclspec -Wall -Wextra -Werror \
            -isystem "$FFX_SDK_ROOT" -c "$bridge/wine_fsr.mm" -o "$work/wine_fsr.o"
        xcrun clang++ -dynamiclib -arch arm64 "$work/wine_heap.o" "$work/wine_fsr.o" \
            -framework Foundation -framework Metal -o "$work/fsr-bridge.so"
        compiler="$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang++"
        headers=(-I "$VULKAN_INCLUDE" -I "$VKD3D_GENERATED_INCLUDE" -I "$VKD3D_INCLUDE" -I "$bridge" -I "$FFX_SDK_ROOT")
        probe="$here/d3d12-fsr.cpp"
        if [[ "$1" == live ]]; then
            : "${VKD3D_HOST_OVERLAY:?Set VKD3D_HOST_OVERLAY to the Wine DLL directory for the patched renderer}"
            : "${FFX_UPSCALE_DLL:?Set FFX_UPSCALE_DLL to the original Windows AMD upscaling DLL}"
            probe="$here/ffx-host-pass.cpp"
            cp "$FFX_UPSCALE_DLL" "$work/amd_fidelityfx_upscaler_original_dx12.dll"
            mv "$work/fsr-bridge.so" "$work/fh6-fsr-bridge.so"
            "$compiler" -std=c++17 -static -shared -O2 "${headers[@]}" "$bridge/fidelityfx-shim.cpp" \
                -luuid -o "$work/amd_fidelityfx_upscaler_dx12.dll"
        fi
        "$compiler" -std=c++17 -static -municode "${headers[@]}" "$probe" "$here/amd-framegen.cpp" \
            -ld3d12 -luuid -o "$work/d3d12-fsr.exe"
        export FH6_NATIVE_FSR4_LIBRARY="$work/libfsr4-arm64.dylib"
        export WINEPREFIX="$work/prefix" WINEDEBUG=fixme-all VKD3D_DEBUG=warn
        export WINEDLLPATH_PREPEND="$engine/renderers/vkd3d/wine"
        if [[ "$1" == live ]]; then
            export WINEDLLPATH_PREPEND="$VKD3D_HOST_OVERLAY:$WINEDLLPATH_PREPEND"
        fi
        export WINEDLLOVERRIDES='d3d12,d3d12core,dxgi=n,b;winemenubuilder,mscoree,mshtml,winedbg=d'
        export VK_DRIVER_FILES="$engine/renderers/kosmickrisp/kosmickrisp_icd.json"
        export VKD3D_FEATURE_LEVEL=12_1 VKD3D_SHADER_MODEL=6_6 VKD3D_CONFIG=no_staggered_submit,force_native_fp16
        trap '"$engine/engine/bin/wineserver" -k >/dev/null 2>&1 || true' EXIT
        cd "$work"
        if [[ "$1" == live ]]; then
            argument=$("$engine/engine/bin/wine" winepath -w "$work/amd_fidelityfx_upscaler_dx12.dll")
        else
            bridge_path=$("$engine/engine/bin/wine" winepath -w "$work/fsr-bridge.so")
            argument='\??\'"${bridge_path%$'\r'}"
        fi
        framegen_path=$("$engine/engine/bin/wine" winepath -w "$framegen_dll")
        "$engine/engine/bin/wine" "$work/d3d12-fsr.exe" "${argument%$'\r'}" \
            "${framegen_path%$'\r'}" 2>&1 | tee "$work/d3d12.log"
        if [[ "$1" == live ]]; then
            grep -q 'FH6 Metal FSR4 dispatch status=0 ' "$work/d3d12.log"
        fi
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
        "$compiler" -Wall -Wextra -Werror -municode "$here/steam-ipc.c" -o "$work/steam-ipc.exe"
        "$compiler" -Wall -Wextra -Werror "$here/steam-ipc-threads.c" -o "$work/steam-ipc-threads.exe"
        export WINEPREFIX="$work/prefix" WINEDLLPATH_PREPEND="$overlay" WINEDEBUG=fixme-all
        export WINEDLLOVERRIDES='winemenubuilder,mscoree,mshtml,winedbg=d;lsteamclient=b;steamclient64=n'
        export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS}"
        trap '"$engine/engine/bin/wineserver" -k >/dev/null 2>&1 || true' EXIT
        cd "$work"
        "$engine/engine/bin/wine" "$work/steam-ipc.exe" 2>&1 | tee "$work/steam.log"
        "$engine/engine/bin/wine" "$work/steam-ipc-threads.exe" 2>&1 | tee "$work/steam-threads.log"
        ;;
    *)
        printf 'Usage: %s metal|d3d12|live|steam\n' "$0" >&2
        exit 2
        ;;
esac
