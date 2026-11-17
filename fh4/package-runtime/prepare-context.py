#!/usr/bin/env python3
"""Emit inactive FH4 process context from verified owned package manifests."""
import argparse, hashlib, json, sys
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from importlib.util import spec_from_file_location,module_from_spec
s=spec_from_file_location('catalog',Path(__file__).resolve().parents[1]/'prepare-package-catalog.py');m=module_from_spec(s);s.loader.exec_module(m)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    catalog=json.loads(a.catalog.read_text());records=catalog['packages']
    if len(records)!=2:raise SystemExit('This candidate supports the FH4 main package and one static framework only')
    verified=[]
    for rec in records:
        root=Path(rec['path']).resolve();manifest=root/'AppxManifest.xml';raw=manifest.read_bytes()
        if sha(manifest)!=rec['manifestSha256'] or m.identity(raw)!=rec['identity']:raise SystemExit('Manifest/catalog mismatch')
        verified.append((root,ET.fromstring(raw),rec['identity']))
    root,xml,ident=verified[0];dep_root,dep_xml,dep=verified[1]
    if ident['Name']!='Microsoft.SunriseBaseGame' or dep['Name']!='Microsoft.VCLibs.140.00':raise SystemExit('Unexpected package graph')
    ns={'p':'http://schemas.microsoft.com/appx/manifest/foundation/windows10'}
    apps=xml.findall('p:Applications/p:Application',ns);deps=xml.findall('p:Dependencies/p:PackageDependency',ns)
    if len(apps)!=1 or len(deps)!=1:raise SystemExit('Ambiguous application/dependency graph')
    app=apps[0].attrib;needed=deps[0].attrib
    if needed['Name']!=dep['Name'] or needed['Publisher']!=dep['Publisher']:raise SystemExit('Dependency identity mismatch')
    if tuple(map(int,dep['Version'].split('.')))<tuple(map(int,needed['MinVersion'].split('.'))):raise SystemExit('Dependency too old')
    exe=(root/app['Executable']).resolve()
    if exe.parent!=root or exe.name!='ForzaHorizon4.exe' or not exe.is_file():raise SystemExit('Unexpected executable')
    if app['EntryPoint']!='Forza.App':raise SystemExit('Unexpected entry point')
    if dep_xml.findtext('p:Properties/p:Framework',namespaces=ns)!='true':raise SystemExit('Dependency is not a framework')
    for rel,digest in catalog['dependencyFiles'].items():
        f=(dep_root/rel).resolve()
        if not f.is_relative_to(dep_root) or sha(f)!=digest:raise SystemExit('Dependency file mismatch: '+rel)
    def win(path):return 'Z:'+str(path).replace('/','\\')
    env=dict(FH4_PACKAGE_EXE=win(exe),FH4_PACKAGE_APP_ID=app['Id'],FH4_PACKAGE_FULL=ident['fullName'],FH4_PACKAGE_PATH=win(root),FH4_PACKAGE_PUBLISHER=ident['Publisher'],FH4_PACKAGE_DEP_FULL=dep['fullName'],FH4_PACKAGE_DEP_PATH=win(dep_root),FH4_PACKAGE_DEP_PUBLISHER=dep['Publisher'])
    result=dict(environment=env,manifestSha256=records[0]['manifestSha256'],dependencyManifestSha256=records[1]['manifestSha256'],dependencyFilesVerified=True,active=False,gameplayVerified=False,licensingModified=False,requires=['matching private kernelbase','catalog imported in private FH4 prefix','UWP activation runtime'])
    with a.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print('Prepared inactive context; manifest and dependency files verified')
if __name__=='__main__':main()
