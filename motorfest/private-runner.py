#!/usr/bin/env python3
"""Motorfest-only native Steam entry. Invoked by Steam, never a second Steam client."""
import fcntl, json, os, sys, subprocess, time
from pathlib import Path

APP_ID='2698940'

def launch_plan(profile, arguments, original):
    data=Path(profile['data']).resolve()
    if data.name!=APP_ID or Path(original.get('STEAM_COMPAT_DATA_PATH','/')).resolve()!=data:
        raise ValueError('Motorfest data-path mismatch')
    if original.get('STEAM_COMPAT_APP_ID',original.get('SteamAppId'))!=APP_ID:
        raise ValueError('Motorfest AppID mismatch')
    if not arguments or arguments[0] not in ('run','waitforexitandrun','getcompatpath'):
        raise ValueError('Unsupported Steam compatibility verb')
    if arguments[0]=='getcompatpath':return None,None
    if len(arguments)<2:raise ValueError('Missing Steam launch target')
    root=Path(profile['candidate']);engine=Path(profile['engine']);entry=root/'steam-entry'
    client=Path(profile['nativeClient'])
    if not (client/'steamclient.dylib').is_file():raise ValueError('Native Steam client missing')
    env={k:v for k,v in original.items() if not k.startswith(('DYLD_','WINE','PROTON_','LINUXHOST_',
         'GRAPHICS_','DXVK_','VKD3D_','CX_','FEX_','ROSETTA_','DXMT_'))}
    env.update(WINEPREFIX=str(data/'pfx'),WINEDEBUG='-all,err+all',
        WINEDLLPATH=':'.join(map(str,[entry/'steam',entry/'overlay',root/'overlay/x86_64-windows',
          root/'overlay/aarch64-windows',root/'overlay',engine/'lib/mullion',engine/'lib/wine'])),
        WINEDLLOVERRIDES='winemenubuilder.exe=d;beclient_x64=b;d3d11,d3d12,d3d12core,dxgi=b;steamclient,steamclient64=n;lsteamclient=b',
        STEAM_COMPAT_CLIENT_INSTALL_PATH=str(client),
        SteamPath='Z:'+str(entry/'steam').replace('/','\\'),GRAPHICS_BACKEND='mullion',
        LINUXHOST_LEGACY_HOST='Z:'+str(root/'host/linuxhost.dll').replace('/','\\'),
        LINUXHOST_LEGACY_BRIDGE=str(root/'runtime/v1/lib64/wine/beclient_x64.dll.so'),
        PROTON_BATTLEYE_RUNTIME=str(root/'runtime'),LINUXHOST_GNU_RUNTIME=str(root/'gnu'),
        LINUXHOST_NATIVE_HELPER=str(root/'host/native-child-helper'),LINUXHOST_AFFINITY_MODE='windows',
        DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN='1',WINE_SIMULATE_WRITECOPY='1')
    if arguments[0]=='waitforexitandrun':
        env.setdefault('SteamAppId',APP_ID)
        env.setdefault('SteamGameId',APP_ID)
    else:
        # Steam's install-script helper must not register as the running game.
        # Otherwise Steam refuses the real launch with AppError_16.
        env.pop('SteamAppId',None)
        env.pop('SteamGameId',None)
    if profile.get('factory7DxgiNative'):
        env['WINEDLLOVERRIDES']=env['WINEDLLOVERRIDES'].replace(
            'd3d11,d3d12,d3d12core,dxgi=b','d3d11,d3d12,d3d12core=b;dxgi=n,b')
    if profile.get('moltenvkAddressSpaceIcd'):
        icd=Path(profile['moltenvkAddressSpaceIcd']).resolve()
        private=data/'.motorfest-private-20261117'
        if not icd.is_relative_to(private) or not icd.is_file():
            raise ValueError('Motorfest private ICD missing or out of scope')
        env['VK_DRIVER_FILES']=str(icd)
        env['VK_ICD_FILENAMES']=str(icd)
    if profile.get('shaderDumpDirectory'):
        dump=Path(profile['shaderDumpDirectory']).resolve()
        if not dump.is_relative_to(data/'.motorfest-private-20261117'):
            raise ValueError('Motorfest shader dump directory out of scope')
        dump.mkdir(parents=True,exist_ok=True)
        env['MVK_CONFIG_SHADER_DUMP_DIR']=str(dump)
    if profile.get('firstFaultDiagnostics') and arguments[0]=='waitforexitandrun':
        diag=data/'.motorfest-private-20261117/diagnostics'
        diag.mkdir(parents=True,exist_ok=True)
        fault=diag/('first-fault-'+str(time.time_ns())+'.bin')
        env['MOTORFEST_FIRST_FAULT_PATH']='Z:'+str(fault).replace('/','\\')
    if profile.get('startupDiagnostics') and arguments[0]=='waitforexitandrun':
        env['WINEDEBUG']='-all,err+all,+seh,+loaddll'
    return [str(engine/'MacOS/wine'),str(entry/'steam/steam.exe'),*arguments[1:]],env

def main():
    profile_path=Path(__file__).with_name('profile.json')
    profile=json.loads(profile_path.read_text())
    if not profile.get('enabled'):raise SystemExit('Motorfest candidate is not enabled')
    command,env=launch_plan(profile,sys.argv[1:],os.environ)
    if command is None:print(profile['data']);return
    # Do not wait on the whole wineserver: Ubisoft background services can outlive
    # the Steam shim. No global Wine/Steam kill and no synthetic success result.
    log=Path(profile['data'])/'motorfest-private-run.log'
    with (Path(profile['data'])/'.notproton-prefix.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        with log.open('ab',buffering=0) as f:
            f.write(('\nMOTORFEST_PRIVATE_ENTRY verb='+sys.argv[1]+' engine=FEX/Mullion gameplayVerified=false\n').encode())
            result=subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT)
            f.write(('MOTORFEST_PRIVATE_EXIT verb='+sys.argv[1]+' status='+str(result.returncode)+'\n').encode())
    raise SystemExit(result.returncode)

if __name__=='__main__':main()
