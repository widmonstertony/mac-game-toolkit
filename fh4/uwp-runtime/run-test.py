#!/usr/bin/env python3
"""Execute only lifecycle.exe in an explicitly supplied, existing test prefix."""
import argparse
import hashlib
import json
import os
import re
from types import SimpleNamespace
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--engine', type=Path, required=True, help='Highball engine root')
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--graphics-engine', type=Path, help='Original renderer/framework root when testing a private Wine-only engine copy')
p.add_argument('--gpu', action='store_true', help='Also probe D3DMetal CoreWindow swap-chain creation and GPU readback')
p.add_argument('--gpu-bridge', action='store_true', help='Use the experimental CoreWindow-to-HWND graphics adapter')
p.add_argument('--gpu-entry', action='store_true', help='Use the app-local DXGI import entry with no explicit factory wrapping')
p.add_argument('--auto-manifest', action='store_true', help='Test Wine startup manifest loading without an explicit activation context')
a = p.parse_args()
if a.gpu_bridge and a.gpu_entry: p.error('Choose explicit bridge or imported entry, not both')
if a.gpu_bridge or a.gpu_entry: a.gpu=True
out = a.build.resolve()
prefix = a.prefix.resolve()
if prefix.name != 'probe-prefix' or not (prefix / 'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix; never a game bottle')
if not (out / 'build.json').is_file():
    raise SystemExit('Missing build receipt')
receipt = json.loads((out / 'build.json').read_text())
if a.gpu_entry and hashlib.sha256((out/'entry/dxgi.dll').read_bytes()).hexdigest()!=receipt['files']['dxgi.dll']:
    raise SystemExit('DXGI entry build hash mismatch')
for name in ('fh4-uwp-runtime.dll', 'lifecycle.exe', *(['gpu-probe.dll','fh4-corewindow-swapchain.dll'] if a.gpu else [])):
    if hashlib.sha256((out / name).read_bytes()).hexdigest() != receipt['files'][name]:
        raise SystemExit('Build hash mismatch: '+name)
graphics = (a.graphics_engine or a.engine).resolve()
fw = graphics / 'frameworks'
env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEMSYNC='1',
           WINEESYNC='0', MVK_CONFIG_LOG_LEVEL='0', WINEDLLOVERRIDES='winemenubuilder.exe=d',
           DYLD_FALLBACK_LIBRARY_PATH=str(fw)+':/usr/lib',
           DYLD_FALLBACK_FRAMEWORK_PATH=str(fw)+':/System/Library/Frameworks')
env.pop('FH4_UWP_REAL_DXGI',None)
if a.gpu:
    renderer=graphics/'renderers/d3dmetal'
    shim=graphics/'renderers/d3dmetal-tsshim'
    paths=':'.join(map(str,[shim/'wine',renderer/'wine',a.engine/'engine/lib/wine']))
    env.update(WINEDEBUG='-all,+loaddll',WINEDLLPATH=paths,WINEDLLPATH_PREPEND=paths,
               WINEDLLOVERRIDES='winemenubuilder.exe=d;d3d12,d3d12core,dxgi=n,b',
               CX_D3DMETALPATH=str(renderer/'external'),D3DM_MTL4='0',
               HB_D3D12_REAL='Z:'+str(shim/'wine/x86_64-windows/apd12.dll').replace('/','\\'),
               DYLD_FALLBACK_LIBRARY_PATH=':'.join(map(str,[renderer/'external',fw,'/usr/lib'])))
    if a.gpu_entry:
        env['FH4_UWP_REAL_DXGI']='Z:'+str(renderer/'wine/x86_64-windows/dxgi.dll').replace('/','\\')
mode = ('gpu-entry' if a.gpu_entry else 'gpu-bridge' if a.gpu_bridge else 'gpu' if a.gpu else 'lifecycle') + ('-auto' if a.auto_manifest else '')
log_path=out/f'run-{mode}.log'
associated=out/'lifecycle.exe.manifest'
entry_dll=out/'dxgi.dll'
if associated.exists():
    raise SystemExit('Refusing existing associated manifest; use a clean build directory')
if entry_dll.exists():
    raise SystemExit('Refusing existing app-local dxgi.dll; use a clean build directory')
try:
    if a.gpu_entry: entry_dll.write_bytes((out/'entry/dxgi.dll').read_bytes())
    if a.auto_manifest: associated.write_bytes((out/'runtime.manifest').read_bytes())
    with log_path.open('w') as f:
        try:
            result = subprocess.run([str(a.engine / 'engine/bin/wine'), str(out / 'lifecycle.exe'),
                                *(['--gpu-bridge' if a.gpu_bridge else '--gpu'] if a.gpu else []),
                                *(['--auto-manifest'] if a.auto_manifest else [])],
                                    cwd=out, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=60)
        except subprocess.TimeoutExpired:
            result=SimpleNamespace(returncode=124)
finally:
    if a.auto_manifest: associated.unlink(missing_ok=True)
    if a.gpu_entry and entry_dll.exists():
        if hashlib.sha256(entry_dll.read_bytes()).hexdigest()==receipt['files']['dxgi.dll']:
            entry_dll.unlink()
log = log_path.read_text(errors='replace')
life_passed = 'LIFECYCLE_PASS' in log
dispatch_passed = log.count('DISPATCH_ACTION_PASS') == 2 and log.count('DISPATCH_SHUTDOWN_PASS') == 2
window_passed = log.count('WINDOW_STATE_PASS') == 6
passed = result.returncode == 0 and life_passed and dispatch_passed and window_passed
gpu_hr = re.search(r'GPU_COREWINDOW_RESULT=([0-9a-f]{8})',log)
(out / f'result-{mode}.json').write_text(json.dumps(dict(passed=passed, exitCode=result.returncode,
    testedDllSha256=receipt['files']['fh4-uwp-runtime.dll'],
    testedExeSha256=receipt['files']['lifecycle.exe'],
    logSha256=hashlib.sha256(log_path.read_bytes()).hexdigest(),
    lifecyclePassed=life_passed, normalAsyncDispatchVerified=dispatch_passed,
    hiddenWindowVisibilityAndCaptureVerified=window_passed,
    gameplayVerified=False, visiblePresentationVerified=False,
    gpuBridgeUsed=a.gpu_bridge,
    gpuImportedEntryUsed=a.gpu_entry,
    gpuImportedEntryVerified=a.gpu_entry and 'FH4_DXGI_ENTRY CreateDXGIFactory1 hr=00000000' in log,
    rendererInitializedBeforeCallbacks='FH4_DXGI_RENDERER_INIT hr=00000000' in log,
    rendererInitializationCount=log.count('FH4_DXGI_RENDERER_INIT hr=00000000'),
    gpuImportedBothGameFactoriesVerified=a.gpu_entry and all(
        f'FH4_DXGI_ENTRY {name} hr=00000000' in log for name in ('CreateDXGIFactory1','CreateDXGIFactory2')),
    unhandledFaultObserved='Unhandled page fault' in log,
    gpuEntrySha256=receipt['files'].get('dxgi.dll') if a.gpu_entry else None,
    automaticManifestRoutingVerified='AUTOMATIC_MANIFEST_ROUTING_PASS' in log,
    gpuBridgeSha256=receipt['files'].get('fh4-corewindow-swapchain.dll') if a.gpu else None,
    gpuCoreWindowResult=gpu_hr.group(1) if gpu_hr else None,
    gpuReadbackPassed='GPU_COREWINDOW_READBACK_PASS' in log,
    gpuResizePassed='GPU_COREWINDOW_RESIZE_PASS' in log,
    d3dMetalModuleLoaded=bool(re.search(r'build_module Loaded .*apd12\.dll',log)),
    activationEventSubsetVerified=life_passed, activationRoutingVerified=life_passed), indent=2)+'\n')
print('\n'.join(line for line in log.splitlines() if any(t in line for t in ('FAIL','PASS','case=','GPU_','FH4_DXGI_','PROBE_EXCEPTION','Unhandled page fault')))[-4000:])
raise SystemExit(0 if passed else 1)
