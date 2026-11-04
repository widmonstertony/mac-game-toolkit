#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUNTIME_HOME="$HOME/Library/Application Support/com.tencent.yybmac.wine.engine/FH6Runtime"
BOTTLE_DRIVE_C="$RUNTIME_HOME/bottles/FH6-Clean/drive_c"
YYB_ENGINE_HOME="$HOME/Library/Application Support/com.tencent.yybmac.wine.engine"
YYB_LAUNCHER_DIR="$YYB_ENGINE_HOME/wine/drive_c/YYBFH6"
APPS_DB="$YYB_ENGINE_HOME/apps/apps.db"
USER_APPS="$HOME/Applications"
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
MINGW="$(command -v x86_64-w64-mingw32-gcc || true)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/yyb-wine11-bridge.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

[[ -x "$RUNTIME_HOME/bin/highball" ]] || { echo "缺少 $RUNTIME_HOME/bin/highball" >&2; exit 1; }
[[ -f "$RUNTIME_HOME/bottles/FH6-Clean/bottle.json" ]] || { echo "缺少 FH6-Clean 容器" >&2; exit 1; }
[[ -f "$APPS_DB" ]] || { echo "缺少应用宝 apps.db" >&2; exit 1; }
[[ -n "$MINGW" ]] || { echo "缺少 x86_64-w64-mingw32-gcc；请先 brew install mingw-w64" >&2; exit 1; }

TARGETS=(
  'EA|YYBEA11Bridge|应用宝 EA Wine 11桥接器|YYBEA11Launcher.exe|yybea11|EA App|C:\Program Files\Electronic Arts\EA Desktop\EA Desktop\EADesktop.exe'
  'Epic|YYBEpic11Bridge|应用宝 Epic Wine 11桥接器|YYBEpic11Launcher.exe|yybepic11|Epic Games|C:\Program Files (x86)\Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe'
  'Rockstar|YYBRockstar11Bridge|应用宝 Rockstar Wine 11桥接器|YYBRockstar11Launcher.exe|yybrockstar11|Rockstar Games Launcher|C:\Program Files\Rockstar Games\Launcher\Launcher.exe'
)

for target in "${TARGETS[@]}"; do
  IFS='|' read -r key bridge app_name launcher scheme pin windows_path <<< "$target"
  unix_relative="${windows_path#C:\\}"
  unix_relative="${unix_relative//\\//}"
  [[ -f "$BOTTLE_DRIVE_C/$unix_relative" ]] || { echo "容器中缺少 $windows_path" >&2; exit 1; }

  app="$BUILD_DIR/$app_name.app"
  mkdir -p "$app/Contents/MacOS"
  /usr/bin/swiftc -module-cache-path "$BUILD_DIR/module-cache" \
    "$SCRIPT_DIR/macos/Bridge.swift" -o "$app/Contents/MacOS/$bridge"
  cp "$SCRIPT_DIR/macos/$key-Info.plist" "$app/Contents/Info.plist"
  /usr/bin/codesign --force --deep --sign - "$app"

  "$MINGW" -municode -mwindows \
    "-DYYB_URL=L\"$scheme://launch\"" "-DYYB_TITLE=L\"$pin\"" \
    "$SCRIPT_DIR/windows/launcher.c" -o "$BUILD_DIR/$launcher"

  mkdir -p "$USER_APPS"
  /usr/bin/ditto "$app" "$USER_APPS/$app_name.app"
  "$LSREGISTER" -f "$USER_APPS/$app_name.app"
done

mkdir -p "$YYB_LAUNCHER_DIR"
cp "$BUILD_DIR/YYBEA11Launcher.exe" "$YYB_LAUNCHER_DIR/"
cp "$BUILD_DIR/YYBEpic11Launcher.exe" "$YYB_LAUNCHER_DIR/"
cp "$BUILD_DIR/YYBRockstar11Launcher.exe" "$YYB_LAUNCHER_DIR/"

/usr/bin/python3 "$SCRIPT_DIR/install_apps_db.py" "$APPS_DB"

PIN_LIST="$(cd "$RUNTIME_HOME" && HIGHBALL_HOME="$RUNTIME_HOME" ./bin/highball pin list FH6-Clean 2>/dev/null || true)"
for target in "${TARGETS[@]}"; do
  IFS='|' read -r key bridge app_name launcher scheme pin windows_path <<< "$target"
  if ! print -r -- "$PIN_LIST" | /usr/bin/grep -Fq "$pin"; then
    (cd "$RUNTIME_HOME" && HIGHBALL_HOME="$RUNTIME_HOME" ./bin/highball pin add FH6-Clean "$pin" "$windows_path")
  fi
  (cd "$RUNTIME_HOME" && HIGHBALL_HOME="$RUNTIME_HOME" ./bin/highball pin set FH6-Clean "$pin" renderer dxmt)
  (cd "$RUNTIME_HOME" && HIGHBALL_HOME="$RUNTIME_HOME" ./bin/highball pin set FH6-Clean "$pin" env DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN=1)
done

open -gja /Applications/YYBMacApp.app
echo "安装完成：EA、Epic、Rockstar 共用 FH6-Clean Wine 11 容器。"
