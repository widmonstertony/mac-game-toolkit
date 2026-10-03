#!/bin/zsh

set -euo pipefail

PROJECT_DIR="${0:A:h}"
SUPPORT_ROOT="$HOME/Library/Application Support/YYBIntelLauncher"
BIN_DIR="$SUPPORT_ROOT/bin"
SYSTEM_APPS="/Applications"
AGENT="$HOME/Library/LaunchAgents/local.yybintel.launchpad-sync.plist"
DOMAIN="gui/$(id -u)"

mkdir -p "$BIN_DIR" "$HOME/Library/LaunchAgents"
ditto "$PROJECT_DIR/launch-windows-app" "$BIN_DIR/launch-windows-app"
ditto "$PROJECT_DIR/sync-launchpad-apps.py" "$BIN_DIR/sync-launchpad-apps.py"
ditto "$PROJECT_DIR/apply-intel-retina.py" "$BIN_DIR/apply-intel-retina.py"
ditto "$PROJECT_DIR/download-sky.py" "$BIN_DIR/download-sky.py"
ditto "$PROJECT_DIR/windows-app-process.py" "$BIN_DIR/windows-app-process.py"
rm -f "$BIN_DIR/sky-dock-watch.py"
chmod +x "$BIN_DIR/launch-windows-app" "$BIN_DIR/sync-launchpad-apps.py" \
  "$BIN_DIR/apply-intel-retina.py" "$BIN_DIR/download-sky.py" \
  "$BIN_DIR/windows-app-process.py"

clang -fobjc-arc -O2 -Wall -Wextra -framework Cocoa \
  -o "$BIN_DIR/dock-app-host" "$PROJECT_DIR/dock_app_host.m"

if [[ "${YYB_KEEP_WINDOWS_RUNNING:-0}" != "1" ]]; then
  "$BIN_DIR/apply-intel-retina.py"
fi

ditto "$PROJECT_DIR/app-bundles/Steam（Windows）.app-template" "$SYSTEM_APPS/Steam（Windows）.app"
ditto "$PROJECT_DIR/app-bundles/网易游戏启动器.app-template" "$SYSTEM_APPS/网易游戏启动器.app"
ditto "$BIN_DIR/dock-app-host" "$SYSTEM_APPS/Steam（Windows）.app/Contents/MacOS/start"
ditto "$BIN_DIR/dock-app-host" "$SYSTEM_APPS/网易游戏启动器.app/Contents/MacOS/start"
codesign --force --deep --sign - "$SYSTEM_APPS/Steam（Windows）.app" >/dev/null
codesign --force --deep --sign - "$SYSTEM_APPS/网易游戏启动器.app" >/dev/null

ditto "$PROJECT_DIR/launchpad-sync-agent.plist" "$AGENT"
/usr/libexec/PlistBuddy -c "Add :WatchPaths array" "$AGENT"
/usr/libexec/PlistBuddy -c \
  "Add :WatchPaths:0 string $HOME/Library/Application Support/com.tencent.yybmac.wine.engine/wine/drive_c/Program Files (x86)/Steam/steamapps" \
  "$AGENT"
/usr/libexec/PlistBuddy -c \
  "Add :WatchPaths:1 string $HOME/Library/Application Support/com.tencent.yybmac.wine.engine/wine/user.reg" \
  "$AGENT"
launchctl bootout "$DOMAIN" "$AGENT" >/dev/null 2>&1 || true
launchctl bootstrap "$DOMAIN" "$AGENT"
launchctl enable "$DOMAIN/local.yybintel.launchpad-sync"

"$BIN_DIR/sync-launchpad-apps.py"
"$PROJECT_DIR/install-gui.command"

LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -x "$LSREGISTER" ]]; then
  "$LSREGISTER" -f "$SYSTEM_APPS/Steam（Windows）.app"
  "$LSREGISTER" -f "$SYSTEM_APPS/网易游戏启动器.app"
  "$LSREGISTER" -f "$SYSTEM_APPS/Windows 游戏.app"
fi

mdimport -i "$SYSTEM_APPS/Steam（Windows）.app" >/dev/null 2>&1 || true
mdimport -i "$SYSTEM_APPS/网易游戏启动器.app" >/dev/null 2>&1 || true
mdimport -i "$SYSTEM_APPS/Windows 游戏.app" >/dev/null 2>&1 || true

print "系统应用事件同步已启用。Windows 游戏库和新安装的游戏会出现在 macOS 应用列表。"
