#!/usr/bin/env python3
"""Install NFS-only renderer/focus settings in the existing Proton-ish runner."""
import argparse
from pathlib import Path
import subprocess

RENDERER = '''# BEGIN mac-game-toolkit NFS renderer
case "$app_id" in
  1222680) export CX_GRAPHICS_BACKEND=dxmt ;;
  1846380) export CX_GRAPHICS_BACKEND=d3dmetal ;;
esac
# END mac-game-toolkit NFS renderer
'''

FOCUS = '''  # BEGIN mac-game-toolkit Heat fullscreen focus
  if [ "$app_id" = 1222680 ]; then
    export DXMT_CONFIG="${DXMT_CONFIG:+$DXMT_CONFIG;}dxgi.handleAltTab=True"
    printf '%s\\n' "DXMT_CONFIG=$DXMT_CONFIG" >> "$hb_file"
    np_hb_keys="$np_hb_keys DXMT_CONFIG"
    echo "NFS Heat: DXMT fullscreen focus handling enabled; resolution unchanged" >> "$log" 2>&1 || true
  fi
  # END mac-game-toolkit Heat fullscreen focus
'''


def patch(source):
    # Fail on partial installs rather than silently duplicating hooks.
    installed = (RENDERER in source, FOCUS in source)
    if any(installed):
        if not all(installed):
            raise RuntimeError('Partial NFS hooks found; inspect the runner before changing it')
        return source
    anchor = 'echo "app_id=$app_id (STEAM_COMPAT_APP_ID=$STEAM_COMPAT_APP_ID)" >> "$log" 2>&1 || true\n'
    env_anchor = '  np_hb_env_file="$hb_file"\n'
    for item in (anchor, env_anchor):
        if source.count(item) != 1:
            raise RuntimeError('Runner layout changed; refusing an ambiguous patch')
    source = source.replace(anchor, anchor + '\n' + RENDERER, 1)
    return source.replace(env_anchor, env_anchor + FOCUS, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = Path.home() / 'Library/Application Support'
    runner = root / 'Steam/compatibilitytools.d/protonish-highball-x64-crossover26.3-r28/run'
    original = runner.read_text()
    changed = patch(original)
    subprocess.run(['/bin/sh', '-n'], input=changed, text=True, check=True)
    # This installed DXMT must implement the option, not just accept a string.
    dll = root / 'Highball/engines/x64-crossover26.3-r28/renderers/dxmt/wine/x86_64-windows/d3d11.dll'
    if b'dxgi.handleAltTab' not in dll.read_bytes():
        raise RuntimeError('The installed DXMT lacks fullscreen focus handling')
    print('Heat: DXMT fullscreen focus handling. Unbound: D3DMetal (DX12). Other AppIDs unchanged.')
    if not args.apply:
        return
    processes = subprocess.check_output(['/bin/ps', '-axo', 'comm='], text=True).casefold()
    if any(name in processes for name in ('needforspeed', 'eadesktop.exe', 'eabackgroundservice.exe', 'easteamlauncher.exe')):
        raise RuntimeError('Close NFS and EA processes before changing their shared environment')
    if changed != original:
        # Keep only the original small launcher script, never copy a container/game.
        backup = runner.with_name('run.before-nfs-focus-fix')
        if not backup.exists():
            backup.write_text(original)
        runner.write_text(changed)
    print('Installed. Existing resolution, save data, audio and caches were not edited.')


if __name__ == '__main__':
    main()
