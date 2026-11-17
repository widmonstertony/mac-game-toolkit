#!/usr/bin/env python3
"""Prepare a non-active FH4 Store candidate. Never modify or launch the game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--game',type=Path,required=True,help='Existing owned FH4 Content directory')
    p.add_argument('--engine',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    build=a.build.resolve(); game=a.game.resolve(); out=a.out.resolve()
    if out==game or game in out.parents:
        raise SystemExit('Candidate must be outside the game directory')
    manifest=game/'AppxManifest.xml'
    raw=manifest.read_bytes()
    if len(raw)>1024*1024 or b'<!DOCTYPE' in raw.upper():
        raise SystemExit('Unsupported package manifest')
    xml=ET.fromstring(raw)
    ns={'p':'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
    identity=xml.find('p:Identity',ns)
    apps=xml.findall('p:Applications/p:Application',ns)
    app=next((x for x in apps if x.get('Executable')=='ForzaHorizon4.exe'),None)
    if identity is None or identity.get('Name')!='Microsoft.SunriseBaseGame' or app is None:
        raise SystemExit('Requires the owned FH4 Store package, not another game/edition')
    if not (game/'ForzaHorizon4.exe').is_file():
        raise SystemExit('Game executable is missing')
    receipt=json.loads((build/'build.json').read_text())
    test=json.loads((build/'result-gpu-entry-auto.json').read_text())
    for field in ('passed','lifecyclePassed','gpuImportedEntryVerified','rendererInitializedBeforeCallbacks',
                  'automaticManifestRoutingVerified','gpuReadbackPassed','gpuResizePassed','d3dMetalModuleLoaded'):
        if test.get(field) is not True: raise SystemExit('Missing verified gate: '+field)
    artifacts=[('fh4-uwp-runtime.dll','fh4-uwp-runtime.dll','testedDllSha256'),
               ('fh4-corewindow-swapchain.dll','fh4-corewindow-swapchain.dll','gpuBridgeSha256'),
               ('entry/dxgi.dll','dxgi.dll','gpuEntrySha256')]
    for source,name,field in artifacts:
        if sha(build/source)!=receipt['files'][name] or sha(build/source)!=test[field]:
            raise SystemExit('Artifact differs from tested build: '+name)
    renderer=a.engine.resolve()/'renderers/d3dmetal/wine/x86_64-windows/dxgi.dll'
    if not renderer.is_file(): raise SystemExit('Original D3DMetal DXGI is missing')
    out.mkdir(parents=True,exist_ok=False)
    files=out/'files';files.mkdir()
    for source,name,_ in artifacts: shutil.copy2(build/source,files/name)
    activation=(build/'runtime.manifest').read_text().replace(
        'name="Local.UwpLifecycleProbe"','name="Local.FH4StoreCompatibility"')
    ET.fromstring(activation)
    (files/'ForzaHorizon4.exe.manifest').write_text(activation)
    plan=[]
    for item in files.iterdir():
        target=game/item.name
        plan.append(dict(file=item.name,candidateSha256=sha(item),
                         existingSha256=sha(target) if target.is_file() else None))
    data=dict(schema=1,state='staged-not-installed',gameplayVerified=False,installed=False,
              package=dict(name=identity.get('Name'),version=identity.get('Version'),
                           applicationId=app.get('Id'),entryPoint=app.get('EntryPoint')),
              gameDirectory=str(game),packageManifestSha256=sha(manifest),files=plan,
              environmentDelta={'FH4_UWP_REAL_DXGI':'Z:'+str(renderer).replace('/','\\')},
              requiredDllOverride='dxgi=n,b (merge with the FH4-only profile, do not replace unrelated overrides)',
              rendererSha256=sha(renderer),testedEntryResultSha256=sha(build/'result-gpu-entry-auto.json'),
              pending=['UWP window/input events and async dispatch',
                       'Adapter GetParent factory interception',
                       'Package activation and Xbox/cloud-save integration',
                       'Actual FH4 menu/driving and visible presentation validation'])
    (out/'candidate.json').write_text(json.dumps(data,indent=2)+'\n')
    print('Prepared candidate only:',out)
    print('Game files, launcher, save data, shared engine and registry remain untouched.')


if __name__=='__main__': main()
