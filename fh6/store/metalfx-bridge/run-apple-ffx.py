#!/usr/bin/env python3
"""Run the complete FFX candidate on a real GPU in an existing isolated prefix.

The game's existing directory is read-only context for Apple's application-file
capability check. No game is launched and no production file is installed.
"""
import argparse
import importlib.util
import json
import re
import struct
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
p.add_argument('--present-control', action='store_true', help='Exercise the real FFX swapchain and audit composition order/pixels')
p.add_argument('--queued-presents', action='store_true', help='With --present-control, wait for all presents only after the last base frame')
p.add_argument('--production-shim', action='store_true',
               help='Require the installed Store D3D12 shim in the isolated prefix and enable its wrappers')
p.add_argument('--public-ngx-redirect', action='store_true', help='Reproduce public NGX loader/export redirection inside the test candidate')
p.add_argument('--capture-input-control', action='store_true', help='Overwrite tagged inputs after Prepare; verify the actual consumed snapshots and pre-evaluate color on GPU')
a = p.parse_args()
if a.present_control and (a.gap_control or a.missing_ui):
    p.error('--present-control is a separate continuous-frame case')
if a.queued_presents and not a.present_control:
    p.error('--queued-presents requires --present-control')
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
wine, env, original = module.store_test_runtime(prefix, stock_renderer=not a.production_shim)
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
env.update(D3DM_ENABLE_METALFX='1',HB_D3D12_TSSHIM='1' if a.production_shim else '0',WINEDEBUG='-all',
           FH6_APPLE_GAP_CONTROL='1' if a.gap_control else '')
if not a.gap_control:
    env.pop('FH6_APPLE_GAP_CONTROL',None)
env.pop('FH6_APPLE_CREATION_CONTROL',None)
env.pop('FH6_D3DMETAL_NO_ANTILAG2',None)  # This headless probe does not present a window.
env.pop('FH6_APPLE_PRESENT_CONTROL',None)
env.pop('FH6_APPLE_QUEUED_PRESENTS',None)
env.pop('FH6_APPLE_PUBLIC_NGX_REDIRECT',None)
env.pop('FH6_APPLE_CAPTURE_INPUT_CONTROL',None)
if a.capture_input_control:
    env['FH6_APPLE_CAPTURE_INPUT_CONTROL']='1'
    (a.output/'capture-request').write_text('isolated consumed-input overwrite control\n')
if a.public_ngx_redirect:env['FH6_APPLE_PUBLIC_NGX_REDIRECT']='1'
if a.present_control:
    env['FH6_APPLE_PRESENT_CONTROL']='1'
    env['FH6_D3DMETAL_NO_ANTILAG2']='1'
    if a.queued_presents:
        env['FH6_APPLE_QUEUED_PRESENTS']='1'
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
print('\n'.join(l for l in contents.splitlines() if l.startswith(('PASS','FAIL','native','GPU','REAL','PRESENT','[D3DMetal:'))))
passed = rc == 0 and 'PASS MetalFX FFX adapter' in contents
if a.public_ngx_redirect:
    passed = passed and 'PASS public NGX loader/export redirection installed in test candidate only' in contents
loaded_shim = next((line[len('LOADED D3D12 '):].strip() for line in contents.splitlines()
                    if line.startswith('LOADED D3D12 ')), None)
# Hashing a file in the prefix alone does not prove the loader used it.
passed = passed and loaded_shim is not None and loaded_shim.lower() == r'c:\windows\system32\d3d12.dll'
if a.present_control:
    passed = passed and 'PASS MetalFX FFX scheduler ordered midpoint/original GPU pixels' in contents
capture_observation=[]
if a.capture_input_control and passed:
    captures=sorted((a.output/'captures').glob('*/capture.json'))
    if len(captures)!=2:raise RuntimeError('Require exactly two fence-completed consumed-input captures')
    for receipt in captures:
        m=json.loads(receipt.read_text());images={i['name']:i for i in m['images']}
        expected={'depth','motion','scene','ui','present','generated-before-ui','generated-after-ui',
                  'consumed-prepare-depth','consumed-prepare-motion','scene-before-fg'}
        if set(images)!=expected or not m['gpu_fence_completed'] or not m['pixel_files_complete']:
            raise RuntimeError('Incomplete consumed-input capture')
        frame=m['frame'];pixels={n:(receipt.parent/(n+'.bin')).read_bytes() for n in expected}
        for name,i in images.items():
            if len(pixels[name])!=i['bytes'] or i['bytes']!=i['height']*i['row_pitch']:
                raise RuntimeError('Capture layout mismatch: '+name)
        for name in ('consumed-prepare-depth','consumed-prepare-motion','scene-before-fg'):
            i=images[name]
            if i['source_frame']!=frame or i['copy_stage']!='before-native-evaluate':
                raise RuntimeError('Missing consumed-input provenance: '+name)
        for original_name,snapshot in (('depth','consumed-prepare-depth'),('motion','consumed-prepare-motion')):
            if images[original_name]['ptr']==images[snapshot]['ptr']:
                raise RuntimeError('Consumed snapshot aliases the overwritten original')
        width,height=images['scene']['width'],images['scene']['height']
        if (width,height)!=(192,128):raise RuntimeError('Unexpected synthetic fixture dimensions')
        for y in range(height):
            for x in range(width):
                car=20+frame*2<=x<52+frame*2 and 40<=y<72
                values={'depth':struct.pack('<f',.99),'motion':struct.pack('<HH',0x3c00,0x3c00),
                        'consumed-prepare-depth':struct.pack('<f',.8 if car else .2),
                        'consumed-prepare-motion':struct.pack('<HH',0xc000 if car else 0,0),
                        'scene-before-fg':bytes((100,30,20,255) if car else (25,40,60,255))}
                for name,value in values.items():
                    off=y*images[name]['row_pitch']+x*4
                    if pixels[name][off:off+4]!=value:
                        raise RuntimeError(f'Wrong consumed epoch: frame={frame} image={name} pixel={x},{y}')
        for y in range(height):
            before=y*images['scene-before-fg']['row_pitch'];after=y*images['scene']['row_pitch']
            if pixels['scene-before-fg'][before:before+width*4]!=pixels['scene'][after:after+width*4]:
                raise RuntimeError('Native evaluation altered its source color')
        capture_observation.append({'frame':frame,'images':len(images),'consumed_pixels_checked':width*height*5,
                                    'originals_overwritten_after_prepare':True,'scene_unchanged_by_evaluate':True})
    print('PASS exact consumed snapshots versus overwritten original GPU pixels',capture_observation)
scheduler_summary = re.search(r'PRESENT summary callbacks=(\d+) dispatches=(\d+) native_pairs=(\d+) errors=(\d+) ui_errors=(\d+) original_errors=(\d+)', contents)
result = {'exit':rc,'passed':passed,'missing_ui':a.missing_ui,'gap_control':a.gap_control,
          'candidate_sha256':module.sha(candidate),'original_sha256':module.sha(original),
          'native_carrier_sha256':module.sha(a.carrier/'x86_64-windows/nvngx.dll'),
          'game_marker_sha256':module.sha(a.game_dir/'nvngx_dlssg.dll'),
          'stock_renderer_shim':not a.production_shim,
          'renderer_shim_sha256':module.sha(prefix/'drive_c/windows/system32/d3d12.dll'),
          'shim_wrappers_enabled':a.production_shim,
          'loaded_d3d12':loaded_shim,
          'scheduler_present_test':a.present_control,
          'public_ngx_redirection_test':a.public_ngx_redirect,
          'queued_presents':a.queued_presents,
          'consumed_input_capture_control':capture_observation,
          'synthetic_scheduler_verified':bool(a.present_control and passed and scheduler_summary),
          'scheduler_observation':dict(zip(('callbacks','dispatches','native_pairs','errors','ui_errors','original_errors'),
                                          map(int,scheduler_summary.groups()))) if scheduler_summary else None,
          'live_presentation_verified':False,'game_quality_verified':False}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
if not passed:
    raise SystemExit(1)
