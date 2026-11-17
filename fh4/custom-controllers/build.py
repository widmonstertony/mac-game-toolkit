#!/usr/bin/env python3
"""Build a private production DLL and a separate diagnostic-export variant."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
for name in ('source','build','out','cc'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--tools-dir',type=Path,action='append',default=[])
a=p.parse_args()
source=a.source/'dlls/windows.gaming.input'
manager=source/'manager.c';spec=source/'windows.gaming.input.spec'
body=manager.read_text();exports=spec.read_text()
if '#include "custom-factories.inc"' not in body or 'registry-test.inc' in body:
    p.error('Requires the patched private source, not an active test variant')
if (source/'custom-factories.inc').read_bytes()!=(ROOT/'custom-factories.inc').read_bytes():
    p.error('Registry source differs from this build input')
a.out.mkdir(parents=True,exist_ok=False)
env=dict(os.environ)
env['PATH']=':'.join(map(str,[*a.tools_dir,a.cc.absolute().parent]))+':'+env['PATH']
target='dlls/windows.gaming.input/x86_64-windows/windows.gaming.input.dll'
def build(log,destination):
    # Source/spec swaps can happen within one filesystem timestamp tick. Force
    # this one translation unit and link to prevent a stale diagnostic export
    # from being mislabeled as a production DLL.
    (a.build/'dlls/windows.gaming.input/x86_64-windows/manager.o').unlink(missing_ok=True)
    (a.build/target).unlink(missing_ok=True)
    with (a.out/log).open('w') as f:
        subprocess.run(['make','-j4',target],cwd=a.build,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    shutil.copy2(a.build/target,a.out/destination)
build('production.log','candidate.dll')
try:
    shutil.copy2(ROOT/'registry-test.inc',source/'registry-test.inc')
    manager.write_text(body+'\n#include "registry-test.inc"\n')
    spec.write_text(exports+'\n@ stdcall FH4CustomControllerSelfTest()\n')
    build('test.log','registry-test.dll')
finally:
    manager.write_text(body);spec.write_text(exports)
# Restore the production build output as well as the source/spec.
build('production-restored.log','candidate.dll')
reader=a.cc.absolute().parent/'llvm-readobj'
export_names={}
for name in ('candidate.dll','registry-test.dll'):
    report=subprocess.check_output([str(reader),'--coff-exports',str(a.out/name)],text=True)
    export_names[name]=re.findall(r'^  Name: (.+)$',report,re.M)
expected=['DllCanUnloadNow','DllGetActivationFactory','DllGetClassObject']
if sorted(export_names['candidate.dll'])!=expected or sorted(export_names['registry-test.dll'])!=expected+['FH4CustomControllerSelfTest']:
    raise SystemExit('Unexpected production/test export split; do not install these artifacts')
subprocess.run([str(a.cc.absolute()),'-O2',str(ROOT/'test-runner.c'),'-o',str(a.out/'test-runner.exe')],check=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
receipt=dict(gameplayVerified=False,installed=False,sourceSha256=sha(manager),
             exports=export_names,
             files={name:sha(a.out/name) for name in ('candidate.dll','registry-test.dll','test-runner.exe')},
             inputs={name:sha(ROOT/name) for name in ('custom-factories.inc','registry-test.inc','apply-source.py','test-runner.c')})
(a.out/'build.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(a.out)
