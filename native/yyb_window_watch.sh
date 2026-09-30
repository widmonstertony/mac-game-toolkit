#!/bin/zsh
set -u

support_root="$HOME/Library/Application Support/SkyYYBMacFix"
engine_apps=("$HOME/Library/Application Support/com.tencent.yybmac/ExeEngineDownload/"*.app(Nom))
(( ${#engine_apps} )) || exit 1
engine_root="$engine_apps[1]/Contents"
prefix="$HOME/Library/Application Support/com.tencent.yybmac.wine.engine/wine"
loader="$support_root/bin/wineloader-yyb-helper"
helper_windows='Z:\Library\Application Support\SkyYYBMacFix\bin\yyb-window-fix.exe'
hook="$support_root/lib/libyyb-window-hook.dylib"
address_helper="$support_root/bin/yyb-dlopen-address"
wine_lib="$engine_root/SharedSupport/wine/lib/wine/x86_64-unix"
last_pid=""

while true; do
    pid="$(/usr/bin/pgrep -f 'FeverGamesInstaller\.exe -g FeverGamesLauncher' | /usr/bin/head -n 1 || true)"
    if [[ -z "$pid" ]]; then
        last_pid=""
    elif [[ "$pid" != "$last_pid" && -x "$loader" && -x "$address_helper" && -f "$hook" ]]; then
        # The launcher process exists before its top-level window. Injection
        # is once per PID; the hook performs only a short initial alignment.
        # Its AppKit override then allows ordinary user dragging without a
        # resident position watchdog fighting the pointer.
        for attempt in {1..20}; do
            dlopen_address="$($address_helper 2>/dev/null || true)"
            [[ "$dlopen_address" == 0x* ]] || { /bin/sleep 1; continue; }
            output="$(
                cd "$engine_root" || exit 1
                WINEDEBUG=-all \
                GCOV_PREFIX=/private/tmp/sky-yyb-gcov \
                WINEPREFIX="$prefix" \
                WINEDLLPATH="$wine_lib" \
                "$loader" "$helper_windows" inject "$dlopen_address" "$hook" 2>&1
            )"
            if [[ "$output" == *"injected pid="* ]]; then
                print -r -- "$(date -u +%FT%TZ) launcher pid=$pid loaded owner-scoped top-edge fix"
                last_pid="$pid"
                break
            fi
            /bin/sleep 1
        done
    fi
    /bin/sleep 2
done
