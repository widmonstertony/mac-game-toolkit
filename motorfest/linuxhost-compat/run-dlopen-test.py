#!/usr/bin/env python3
"""Test the host dlopen path on owned code, including missing/modified libraries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True)
p.add_argument('--gnu-runtime',type=Path,required=True)
a=p.parse_args();out=a.build.resolve();prefix=a.prefix.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if prefix.name!='probe-prefix' or not (prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix')
build=json.loads((out/'build.json').read_text())
for name,digest in build['outputs'].items():
    if sha(out/name)!=digest:raise SystemExit('Build hash mismatch: '+name)
fixtures=out/'invalid-runtime';fixtures.mkdir(exist_ok=False)
shutil.copy2(a.gnu_runtime/'libgcc_s.so.1',fixtures/'libgcc_s.so.1')
(fixtures/'libstdc++.so.6.0.21').write_bytes(b'Intentionally invalid owned test fixture\n')
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
for name in ('LINUXHOST_NATIVE_HELPER','WINEDLLPATH','WINEDLLPATH_PREPEND','PROTON_LINUX_HOST','PROTON_BATTLEYE_RUNTIME','LINUXHOST_AFFINITY_MODE',
             'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FALLBACK_FRAMEWORK_PATH','LINUXHOST_GNU_RUNTIME'):
    env.pop(name,None)
cases=[('disabled',None),('missing',str(out/'nonexistent-runtime')),('modified',str(fixtures)),
       ('valid',str(a.gnu_runtime.resolve())),('dll-valid',str(a.gnu_runtime.resolve())),
       ('dll-modified',str(fixtures))]
results={}
for name,runtime in cases:
    test_env=dict(env)
    if runtime:test_env['LINUXHOST_GNU_RUNTIME']=runtime
    log=out/('dlopen-'+name+'.log')
    command=[str(out/'dlopen-probe.exe'),str(out/'cxx-probe.so')]
    if name.startswith('dll-'):
        dll_path='Z:'+str(out/'linuxhost.dll').replace('/','\\')
        command=[str(out/'dll-probe.exe'),dll_path,str(out/'cxx-probe.so')]
    with log.open('w') as f:
        try:
            rc=subprocess.run([str(a.engine/'MacOS/wine'),*command],
                env=test_env,stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        except subprocess.TimeoutExpired:rc=124
    text=log.read_text(errors='replace')
    if name in ('valid','dll-valid'):
        marker='DLL_CXX_PASS result=0' if name.startswith('dll-') else 'DLOPEN_CXX_PASS result=0'
        passed=rc==0 and marker in text and text.count('CXX_CONSTRUCTOR_RAN')==1
    elif name=='dll-modified':
        passed=rc==10 and 'DLL_DLOPEN_REJECT GNU runtime verification failed' in text and 'CXX_CONSTRUCTOR_RAN' not in text
    else:
        passed=rc==10 and 'DLOPEN_REPEAT_REJECT' in text and 'CXX_CONSTRUCTOR_RAN' not in text
        passed=passed and ('imports incomplete' in text if name=='disabled' else 'verification failed' in text)
    results[name]={'exitCode':rc,'passed':passed,'logSha256':sha(log)}
    print(name,results[name],text[-1200:],flush=True)
receipt={'tests':results,'allPassed':all(v['passed'] for v in results.values()),
    'buildReceiptSha256':sha(out/'build.json'),'runnerSha256':sha(Path(__file__)),
    'clientCodeExecuted':False,'installed':False,'gameplayVerified':False}
(out/'dlopen-validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['allPassed'] else 1)
