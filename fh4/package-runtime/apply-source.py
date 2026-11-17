#!/usr/bin/env python3
"""Apply the candidate to an isolated Highball source copy; never an installed engine."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);a=p.parse_args();root=a.source.resolve();here=Path(__file__).resolve().parent
if 'fh4-package-runtime-' not in str(root):raise SystemExit('Use an isolated fh4-package-runtime-* build directory')
paths={n:root/n for n in ['dlls/kernelbase/version.c','dlls/kernelbase/loader.c','include/winbase.h']}
old={n:f.read_text() for n,f in paths.items()};new=dict(old)
s=new['dlls/kernelbase/version.c'];start=s.index('/***********************************************************************\n *         GetCurrentApplicationUserModelId');end=s.index('/***********************************************************************\n *         GetPackageFullName',start)
if s[start:end].count('stub')!=6:raise SystemExit('Unexpected existing package implementation')
s=s[:start]+'#include "fh4-package-context.inc"\n\n'+s[end:]
start=s.index('LONG WINAPI GetPackagePathByFullName(');end=s.index('static const struct',start)
if 'stub' not in s[start:end]:raise SystemExit('Unexpected path implementation')
new['dlls/kernelbase/version.c']=s[:start]+s[end:]
s=new['dlls/kernelbase/loader.c'];start=s.index('HMODULE WINAPI /* DECLSPEC_HOTPATCH */ LoadPackagedLibrary');end=s.index('/***********************************************************************\n *      LoadAppInitDlls',start)
if 'APPMODEL_ERROR_NO_PACKAGE' not in s[start:end] or 'semi-stub' not in s[start:end]:raise SystemExit('Unexpected loader implementation')
new['dlls/kernelbase/loader.c']=s[:start]+'#include "fh4-packaged-loader.inc"\n\n'+s[end:]
pattern='#if defined(__x86_64__) && defined(__GNUC__) && !defined(__arm64ec__) && !defined(WINE_UNIX_LIB)'
replacement='#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__)) && !defined(__arm64ec__) && !defined(WINE_UNIX_LIB)'
if new['include/winbase.h'].count(pattern)!=2:raise SystemExit('Expected Highball LastError synchronization patch')
new['include/winbase.h']=new['include/winbase.h'].replace(pattern,replacement)
receipt={}
for n,f in paths.items():
    backup=f.with_name(f.name+'.before-fh4-package')
    if backup.exists():raise SystemExit('Existing backup; refusing repeated application')
for n,f in paths.items():
    f.with_name(f.name+'.before-fh4-package').write_text(old[n]);f.write_text(new[n])
    receipt[n]={'before':hashlib.sha256(old[n].encode()).hexdigest(),'after':hashlib.sha256(new[n].encode()).hexdigest()}
for name,target in [('package-context.inc','fh4-package-context.inc'),('packaged-loader.inc','fh4-packaged-loader.inc')]:
    (root/'dlls/kernelbase'/target).write_bytes((here/name).read_bytes())
(root/'fh4-package-source.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('Patched isolated source; no binary installed')
