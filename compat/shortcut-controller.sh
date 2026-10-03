#!/bin/zsh
set -eu

mode="$1"
original="$2"
shift 2

user_home="$HOME"

# YYB 0.8.0 can generate a standalone Sky bundle that always fails with
# errCode:-1 on the verified M4 setup, even while Fever is already running.
# Keep the Launchpad icon useful by routing it to the working authenticated
# Fever flow.  Do this before the legacy injected-shim checks: exact M4 systems
# use the verified winevulkan patch and intentionally have no shim.
if [[ "$mode" == "child" ]]; then
    child_app="${original%%.app/*}.app"
    shortcuts_root="${child_app:h}"
    sibling_parent="$shortcuts_root/com.tencent.macexe.com.45a7ca33.app"
    internal_parent="$user_home/Library/Application Support/com.tencent.yybmac/Applications/com.tencent.macexe.com.45a7ca33.app"
    public_parent="/Applications/腾讯应用宝/com.tencent.macexe.com.45a7ca33.app"
    parent_running=0
    for candidate in "$internal_parent" "$public_parent" "$sibling_parent"; do
        parent_executable="$candidate/Contents/MacOS/YYBPackage"
        if [[ -x "$parent_executable" ]] && /usr/bin/pgrep -f "$parent_executable" >/dev/null 2>&1; then
            parent_running=1
            break
        fi
    done
    if [[ -d "$sibling_parent" ]]; then
        # Wine 1.2.4 asks LaunchServices to open the generated child app before
        # it lets an already-created sky.exe process create its Cocoa
        # application.  Only that parent+sky state is the engine handshake:
        # executing the original child host acknowledges it.  A normal user
        # click has no sky.exe yet and must still be routed to Fever, even when
        # Fever happens to be open.  Routing the engine handshake back to Fever
        # makes macdrv wait forever at "launch intercepted" while Fever reports
        # the game as running with no window.
        if (( parent_running )) && \
            /usr/bin/pgrep -fi 'sky[.]exe' >/dev/null 2>&1; then
            # YYB's native child host uses argv[0] as part of its engine IPC
            # identity. Executing the renamed backup with its on-disk name
            # returns errCode:-1 even though the bytes are unchanged. Preserve
            # the original process name while still keeping the wrapper at the
            # bundle's declared executable path.
            exec -a YYBPackage "$original" "$@"
        fi
        # Open the exact internal copy. YYB mirrors the same bundle id under
        # /Applications; asking LaunchServices for that id on a cold start can
        # launch the mirror and the internal original, producing two Fever
        # instances. Opening this path also focuses it when it is already up.
        parent_app="$sibling_parent"
        [[ -d "$internal_parent" ]] && parent_app="$internal_parent"
        /usr/bin/open "$parent_app"
        exit 0
    fi
    exec "$original" "$@"
fi

prefix="$user_home/Library/Application Support/com.tencent.yybmac.wine.engine/wine"
engine_root="$user_home/Library/Application Support/com.tencent.yybmac/ExeEngineDownload"
shim="$user_home/Library/Application Support/SkyYYBMacFix/compat/libSkyYYBGPUCompat.dylib"

engine=""
for candidate in "$engine_root"/*.app(Nom); do
    if [[ -x "$candidate/Contents/MacOS/wineserver" && -f "$candidate/Contents/Frameworks/libMoltenVK.dylib" ]]; then
        engine="$candidate"
        break
    fi
done

if [[ -z "$engine" || ! -x "$original" || ! -f "$shim" ]]; then
    print -u2 "Sky YYB fix: M4 compatibility files are incomplete. Run install.command again."
    exit 75
fi

compat_engine_running() {
    local pid
    for pid in $(/usr/bin/pgrep -f "$engine/Contents/MacOS/wineserver" 2>/dev/null || true); do
        if /usr/sbin/lsof -p "$pid" -Fn 2>/dev/null | /usr/bin/grep -Fq "n$shim"; then
            return 0
        fi
    done
    return 1
}

if ! compat_engine_running; then
    WINEPREFIX="$prefix" "$engine/Contents/MacOS/wineserver" -k >/dev/null 2>&1 || true
    /bin/sleep 1
    /usr/bin/open -n \
        --env "WINEPREFIX=$prefix" \
        --env "SKY_YYB_GPU_COMPAT=1" \
        --env "DYLD_INSERT_LIBRARIES=$shim" \
        "$engine"

    ready=0
    for _ in {1..40}; do
        if compat_engine_running; then
            ready=1
            break
        fi
        /bin/sleep 0.25
    done
    if (( ! ready )); then
        print -u2 "Sky YYB fix: the compatible Wine engine did not start."
        exit 76
    fi
    /bin/sleep 3
fi

exec "$original" "$@"
