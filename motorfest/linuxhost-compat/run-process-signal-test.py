#!/usr/bin/env python3
"""Exercise owned ELF signals and actual native child completion in isolated Wine."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--prefix',type=Path,required=True)
a=p.parse_args();out=a.build.resolve();prefix=a.prefix.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if prefix.name!='probe-prefix' or not (prefix/'system.reg').is_file():raise SystemExit('Requires isolated probe-prefix')
build=json.loads((out/'build.json').read_text())
for name,digest in build['outputs'].items():
    if sha(out/name)!=digest:raise SystemExit('Build hash mismatch: '+name)
env=dict(os.environ,WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
for name in list(env):
    if name.startswith(('DYLD_','PROTON_','WINEDLLPATH','LINUXHOST_')):env.pop(name,None)
results={}
for entry in ('signal_test','signal_default','process_test','wait_test','signal_syscall_test'):
    log=out/(entry+'.log')
    runenv=dict(env)
    runenv['LINUXHOST_NATIVE_HELPER']=str(out/'native-child-helper')
    runenv['LINUXHOST_WAIT_CHILD']=str(out/'wait-child.exe')
    with log.open('w') as f:
        try:rc=subprocess.run([str(a.engine/'MacOS/wine'),str(out/'dll-probe.exe'),
            'Z:'+str(out/'linuxhost.dll').replace('/','\\'),str(out/'process-signal.so'),entry],
            env=runenv,cwd=out,stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        except subprocess.TimeoutExpired:rc=124
    text=log.read_text(errors='replace')
    passed=(rc==143 and 'DLL_CALL_' not in text) if entry=='signal_default' else (rc==0 and 'DLL_CALL_PASS result=0' in text)
    results[entry]={'exitCode':rc,'passed':passed,'logSha256':sha(log)}
    print(entry,results[entry],text[-1500:],flush=True)
    if rc==124:break # Initialization hang: do not stack additional processes.
receipt={'tests':results,'allPassed':all(r['passed'] for r in results.values()),
    'buildReceiptSha256':sha(out/'build.json'),'runnerSha256':sha(Path(__file__)),
    'scope':'Synchronous guest signals only; explicit macOS shell supervisor with Linux wait-status translation. Not a Linux shell namespace or asynchronous fault delivery.',
    'clientCodeExecuted':False,'installed':False,'gameplayVerified':False}
(out/'process-signal-validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['allPassed'] else 1)
