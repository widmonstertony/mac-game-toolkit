#!/usr/bin/env python3
"""Build isolated native-Steam forwarding files; never edit a Steam/game install.

Uses the user's existing Proton-ish bridge. Named exports forward to its real
lsteamclient; no API result, ownership or authentication value is substituted.
"""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for key in ('bridge','toolchain','out'):p.add_argument('--'+key,type=Path,required=True)
a=p.parse_args();out=a.out.resolve();bridge=a.bridge.resolve();cc=a.toolchain/'bin/x86_64-w64-mingw32-clang'
inputs=['steam.exe','steamclient.dll','tier0_s.dll','vstdlib_s.dll','tier0_s64.dll','vstdlib_s64.dll','x86_64-windows/lsteamclient.dll','i386-windows/lsteamclient.dll','aarch64-unix/lsteamclient.so']
for name in inputs:
    if not (bridge/name).is_file():raise SystemExit('Missing bridge input: '+name)
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
out.mkdir(parents=True,exist_ok=False)
steam=out/'steam';steam.mkdir()
for name in inputs:
    if '/' not in name:shutil.copy2(bridge/name,steam/name)
for arch in ('x86_64-windows','aarch64-windows'):
    dst=out/'overlay'/arch;dst.mkdir(parents=True)
    shutil.copy2(bridge/'x86_64-windows/lsteamclient.dll',dst/'lsteamclient.dll')
for arch in ('i386-windows','aarch64-unix'):
    dst=out/'overlay'/arch;dst.mkdir(parents=True)
    name='lsteamclient.so' if arch.endswith('unix') else 'lsteamclient.dll'
    shutil.copy2(bridge/arch/name,dst/name)
for src in ('x86_64-windows/lsteamclient.dll','aarch64-unix/lsteamclient.so'):shutil.copy2(bridge/src,steam/Path(src).name)
readobj=a.toolchain/'bin/llvm-readobj'
exports=subprocess.check_output([str(readobj),'--coff-exports',str(bridge/'x86_64-windows/lsteamclient.dll')],text=True)
names=re.findall(r'^  Name: ([A-Za-z_][A-Za-z_0-9]*)$',exports,re.M)
if not {'CreateInterface','Steam_BGetCallback','Steam_FreeLastCallback'}.issubset(names):raise SystemExit('Incompatible bridge exports')
definition=out/'steamclient64.def'
definition.write_text('LIBRARY steamclient64\nEXPORTS\n'+''.join(f'{n}=lsteamclient.{n}\n' for n in names))
# Proton-ish allocates interface wrappers inside steamclient64's .data section.
# Supply dedicated initialized storage, without CRT state that it could overwrite.
source=out/'steamclient64.c'
source.write_text('unsigned char bridge_interface_storage[1024*1024]={1};\nint bridge_entry(void *m,unsigned reason,void *r){(void)m;(void)reason;(void)r;return 1;}\n')
subprocess.run([str(cc),'-shared','-nostdlib','-fno-stack-protector','-Wl,--entry,bridge_entry',str(source),str(definition),'-o',str(steam/'steamclient64.dll')],check=True)
# Ubisoft's game starter is i386 even when the game is x64. Forward its
# original Steam API to the same real native client, through the i386 bridge.
exports32=subprocess.check_output([str(readobj),'--coff-exports',str(bridge/'i386-windows/lsteamclient.dll')],text=True)
names32=re.findall(r'^  Name: ([A-Za-z_][A-Za-z_0-9]*)$',exports32,re.M)
if not {'CreateInterface','Steam_BGetCallback','Steam_FreeLastCallback'}.issubset(names32):raise SystemExit('Incompatible i386 bridge exports')
definition32=out/'steamclient.def'
definition32.write_text('LIBRARY steamclient\nEXPORTS\n'+''.join(f'{n}=lsteamclient.{n}\n' for n in names32))
source32=out/'steamclient.c'
source32.write_text('unsigned char bridge_interface_storage[1024*1024]={1};\nint __stdcall bridge_entry(void *m,unsigned reason,void *r){(void)m;(void)reason;(void)r;return 1;}\n')
subprocess.run([str(a.toolchain/'bin/i686-w64-mingw32-clang'),'-shared','-nostdlib','-fno-stack-protector','-Wl,--entry,_bridge_entry@12',str(source32),str(definition32),'-o',str(steam/'steamclient.dll')],check=True)
subprocess.run([str(cc),str(Path(__file__).with_name('steam-entry-probe.c')),'-ladvapi32','-o',str(out/'steam-entry-probe.exe')],check=True)
receipt={'bridgeInputs':{n:sha(bridge/n) for n in inputs},'outputs':{str(f.relative_to(out)):sha(f) for f in out.rglob('*') if f.is_file()},'builderSha256':sha(Path(__file__)),'probeSourceSha256':sha(Path(__file__).with_name('steam-entry-probe.c')),'exportCount':len(names),'installed':False,'gameplayVerified':False}
(out/'steam-entry-build.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(out)
