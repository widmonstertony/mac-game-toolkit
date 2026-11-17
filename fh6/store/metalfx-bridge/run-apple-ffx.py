#!/usr/bin/env python3
"""Run the complete FFX candidate on a real GPU in an existing isolated prefix.

The game's existing directory is read-only context for Apple's application-file
capability check. No game is launched and no production file is installed.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--artifacts', type=Path, required=True)
p.add_argument('--carrier', type=Path, required=True)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--game-dir', type=Path, default=Path.home()/'Games/Windows/Xbox/Forza-Horizon-6')
p.add_argument('--missing-ui', action='store_true')
p.add_argument('--gap-control', action='store_true')
a = p.parse_args()
prefix = a.prefix.resolve(strict=True)
if prefix == Path.home()/'Library/Application Support/MACxus/bottles/xbox-highball':
    p.error('Use the isolated probe prefix, not the running game bottle')
if not (prefix/'system.reg').is_file() or not (a.game_dir/'nvngx_dlssg.dll').is_file():
    p.error('Existing test prefix and genuine game DLSSG file required')
if not (a.carrier/'x86_64-windows/nvngx.dll').is_file():
    p.error('Missing existing Apple NGX carrier')

here = Path(__file__).resolve().parent
helper = here/'../../rosetta/fg-chain/run_native_ui_probe.py'
spec = importlib.util.spec_from_file_location('native_ui_probe', helper.resolve())
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
wine, env, original = module.store_test_runtime(prefix, stock_renderer=True)
manifest = json.loads((a.artifacts/'build-manifest.json').read_text())
for name, digest in manifest['artifacts'].items():
    if module.sha(a.artifacts/name) != digest:
        raise RuntimeError('Changed build artifact: '+name)
candidate = a.artifacts/'framegeneration-apple.dll'
if set(module.pe_imports(candidate)) != set(module.pe_imports(a.artifacts/'framegeneration-audit.dll')):
    raise RuntimeError('Candidate changed startup imports')
a.output.mkdir(parents=True, exist_ok=False)
for name in ('framegeneration-apple.dll','apple-ffx-probe.exe','fh6-fg-chain.asi','sl.interposer.dll'):
    shutil.copy2(a.artifacts/name,a.output/name)
(a.output/'framegeneration-original.dll').symlink_to(original)
win = lambda path: 'Z:'+str(path.resolve()).replace('/', '\\')
for key in ('WINEDLLPATH','WINEDLLPATH_PREPEND'):
    env[key] = str(a.carrier.resolve())+':'+env[key]
env['WINEDLLOVERRIDES'] += ';nvngx=b'
env.update(D3DM_ENABLE_METALFX='1',HB_D3D12_TSSHIM='0',WINEDEBUG='-all',
           FH6_APPLE_GAP_CONTROL='1' if a.gap_control else '')
if not a.gap_control:
    env.pop('FH6_APPLE_GAP_CONTROL',None)
env.pop('FH6_APPLE_CREATION_CONTROL',None)
env.pop('FH6_D3DMETAL_NO_ANTILAG2',None)  # This headless probe does not present a window.
command = [str(wine),str((a.output/'apple-ffx-probe.exe').resolve()),
           win(original),win(a.output/'framegeneration-apple.dll')]
if a.missing_ui:
    command.append('--missing-ui')
with (a.output/'probe.log').open('w') as log:
    try:
        rc = subprocess.run(command,cwd=a.game_dir,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120).returncode
    except subprocess.TimeoutExpired:
        rc = 'timeout'
contents = (a.output/'probe.log').read_text(errors='replace')
print('\n'.join(l for l in contents.splitlines() if l.startswith(('PASS','FAIL','native','GPU','REAL','[D3DMetal:'))))
passed = rc == 0 and 'PASS MetalFX FFX adapter' in contents
result = {'exit':rc,'passed':passed,'missing_ui':a.missing_ui,'gap_control':a.gap_control,
          'candidate_sha256':module.sha(candidate),'original_sha256':module.sha(original),
          'native_carrier_sha256':module.sha(a.carrier/'x86_64-windows/nvngx.dll'),
          'game_marker_sha256':module.sha(a.game_dir/'nvngx_dlssg.dll'),
          'stock_renderer_shim':True,'live_presentation_verified':False,'game_quality_verified':False}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
if not passed:
    raise SystemExit(1)
