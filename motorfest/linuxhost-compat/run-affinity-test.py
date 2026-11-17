#!/usr/bin/env python3
"""Test opt-in Windows logical affinity adaptation, not physical CPU pinning."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True)
a=p.parse_args();out=a.build.resolve();prefix=a.prefix.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if prefix.name!='probe-prefix' or not (prefix/'system.reg').is_file():
    raise SystemExit('Requires an existing isolated probe-prefix')
build=json.loads((out/'build.json').read_text())
for name,digest in build['outputs'].items():
    if sha(out/name)!=digest:raise SystemExit('Build hash mismatch: '+name)
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
for name in ('LINUXHOST_NATIVE_HELPER','WINEDLLPATH','WINEDLLPATH_PREPEND','PROTON_LINUX_HOST','PROTON_BATTLEYE_RUNTIME',
             'DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH','DYLD_FALLBACK_FRAMEWORK_PATH',
             'LINUXHOST_GNU_RUNTIME','LINUXHOST_AFFINITY_MODE'):
    env.pop(name,None)
results={}
for name,mode in [('disabled',None),('invalid-mode','physical'),('logical-windows','windows')]:
    log=out/('affinity-'+name+'.log');case_env=dict(env)
    if mode:case_env['LINUXHOST_AFFINITY_MODE']=mode
    dll_path='Z:'+str(out/'linuxhost.dll').replace('/','\\')
    with log.open('w') as f:
        try:
            rc=subprocess.run([str(a.engine/'MacOS/wine'),str(out/'dll-probe.exe'),
                dll_path,str(out/'affinity.so'),'affinity_test'],env=case_env,
                stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        except subprocess.TimeoutExpired:rc=124
    text=log.read_text(errors='replace')
    if name=='logical-windows':passed=rc==0 and 'DLL_CALL_PASS result=0' in text
    else:passed=rc==10 and 'DLL_DLOPEN_REJECT incomplete ELF imports: 2 unresolved' in text and 'DLL_CALL_' not in text
    results[name]={'exitCode':rc,'passed':passed,'logSha256':sha(log)}
    print(name,results[name],text[-500:],flush=True)
receipt={'tests':results,'allPassed':all(v['passed'] for v in results.values()),
    'buildReceiptSha256':sha(out/'build.json'),'runnerSha256':sha(Path(__file__)),
    'scope':'Windows logical masks: query, restrict, yield, readback, restore, errors, three threads. Default and unsupported modes reject imports.',
    'physicalCpuPinningVerified':False,'defaultEnabled':False,
    'clientCodeExecuted':False,'installed':False,'gameplayVerified':False}
(out/'affinity-validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['allPassed'] else 1)
