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
last_sky_pid=""

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

    # Wine 1.2.4 creates Sky.exe first, then waits for YYB's generated Cocoa
    # child bundle to acknowledge the launch.  LaunchServices may silently
    # choose either the YYB-private bundle or its /Applications mirror because
    # both carry the same bundle id.  Wake one generated child exactly once per
    # Sky process. Invoke the YYB-private wrapper by its exact executable path
    # instead of asking LaunchServices to resolve the duplicate bundle id; the
    # wrapper then preserves YYBPackage as argv[0] while executing the original
    # host, completing macdrv's blocked handshake.
    sky_pid="$(/usr/bin/pgrep -fi 'sky[.]exe' | /usr/bin/head -n 1 || true)"
    if [[ -z "$sky_pid" ]]; then
        last_sky_pid=""
    elif [[ "$sky_pid" != "$last_sky_pid" ]]; then
        child_apps=(
            "$HOME/Library/Application Support/com.tencent.yybmac/Applications/"com.tencent.macexe.com.45a7ca33.*.app(Nom)
            /Applications/腾讯应用宝/com.tencent.macexe.com.45a7ca33.*.app(Nom)
        )
        if (( ${#child_apps} )); then
            child_executable="${child_apps[1]}/Contents/MacOS/YYBPackage"
            [[ -x "$child_executable" ]] && "$child_executable" >/dev/null 2>&1 &!
            print -r -- "$(date -u +%FT%TZ) sky pid=$sky_pid requested generated child host handshake"
            last_sky_pid="$sky_pid"
        fi
    fi
    /bin/sleep 2
done
