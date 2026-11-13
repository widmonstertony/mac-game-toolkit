#!/usr/bin/env python3
"""Prepare only the owned FH6 Store package and its private Highball prefix."""
import argparse
import configparser
import hashlib
import io
import json
import os
from pathlib import Path
import plistlib
import shlex
import shutil
import subprocess
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
EXE_SHA = '0e101d591600b7ce5960579b5bf2eebe55249d5137cdecf02818f3691d6ac8af'
OPTI_SHA = '53df1cd1af1d41ac8dbbe59afaf5c749cb0e74d90b29632bbde778ce81bdc969'
CONFIG_SUFFIX = Path('AppData/Local/ForzaHorizon6/LocalStorage_Shared/ForzaUserConfigSelections/UserConfigSelections')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def prepare(game, apply=False):
    home = Path.home()
    data = home/'Library/Application Support/MACxus'
    root = data/'external-highball'
    prefix = data/'bottles/xbox-highball'
    old = home/'Library/Application Support/protonish/launchers/2483190/rosetta-profile'
    retained = old/'steam-install-retained-20261113'
    game = game.resolve(strict=True)
    manifest = ET.parse(game/'MicrosoftGame.Config').getroot()
    if manifest.find('Identity').get('Name') != 'Microsoft.ForteBaseGame' or manifest.findtext('StoreId') != '9NR1R1XWLCNB':
        raise RuntimeError('Expected the actual owned FH6 Store package')
    if sha(game/'forzahorizon6.exe') != EXE_SHA:
        raise RuntimeError('Store executable build changed; review compatibility first')
    if not (prefix/'system.reg').is_file():
        raise RuntimeError('Prepare the real GDK-enabled Store prefix first')
    audit = json.loads((old/'fg-chain-audit.json').read_text())
    history = json.loads((old/'fg-native-ui.json').read_text())
    if not history['gpu_verification']['passed']:
        raise RuntimeError('The generated-frame candidate has not passed GPU verification')
    oldgpu = old/'prefix/drive_c/highball'
    mappings = {
        retained/'version.dll': (game/'version.dll', OPTI_SHA),
        retained/'fakenvapi.dll': (game/'fakenvapi.dll', '373e73dc06e2d66e90083fde3bc99ef5c1ed0bef98d1f7eda56c8b073269c3f1'),
        oldgpu/'fg-chain/amd_fidelityfx_upscaler_dx12.dll': (prefix/'drive_c/highball/fg-chain/amd_fidelityfx_upscaler_dx12.dll', audit['artifacts']['upscaler-audit.dll']),
        retained/'OptiScaler/amd_fidelityfx_upscaler_dx12.dll': (prefix/'drive_c/highball/fg-chain/upscaler-original.dll', 'aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace'),
        oldgpu/'fg-native-ui/amd_fidelityfx_framegeneration_dx12.dll': (prefix/'drive_c/highball/fg-native-ui/amd_fidelityfx_framegeneration_dx12.dll', history['candidate_sha256']),
        retained/'OptiScaler/amd_fidelityfx_framegeneration_dx12.dll': (prefix/'drive_c/highball/fg-native-ui/framegeneration-original.dll', history['original_provider_sha256']),
        retained/'OptiScaler-FH6-SDK/fh6-fg-chain.asi': (prefix/'drive_c/highball/store-plugins/fh6-fg-chain.asi', audit['artifacts']['fh6-fg-chain.asi']),
        retained/'OptiScaler-FH6-SDK/fh6-utf8-dialog.asi': (prefix/'drive_c/highball/store-plugins/fh6-utf8-dialog.asi', 'bd10eb5e796cd5d21cbd390c61c7015ffb00cbed1d4b4d27e7c8764d91d7bc6f'),
    }
    for src, (dst, expected) in mappings.items():
        if sha(src) != expected:
            raise RuntimeError('Retained component does not match its verified source: '+src.name)
        if dst.exists() and sha(dst) != expected:
            raise RuntimeError('Refusing to replace an unknown Store component: '+str(dst))
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read_string((retained/'OptiScaler.ini').read_text())
    ini['Plugins']['Path'] = r'C:\highball\store-plugins'
    ini['Libraries']['FfxDx12SRPath'] = r'C:\highball\fg-chain'
    ini['Libraries']['FfxDx12FGPath'] = r'C:\highball\fg-native-ui'
    # The retained game selection requests NGX mode 0. Its old 1.4 override is
    # not the FSR Quality ratio; explicitly use 1.5, regardless of the input label.
    ini['QualityOverrides']['QualityRatioOverrideEnabled'] = 'true'
    for key in ['QualityRatioQuality','QualityRatioBalanced','QualityRatioPerformance','QualityRatioUltraPerformance']:
        ini['QualityOverrides'][key] = '1.5'
    stream = io.StringIO(); ini.write(stream)
    sources = list((old/'prefix/drive_c/users').glob('*/'+str(CONFIG_SUFFIX)))
    if len(sources)!=1: raise RuntimeError('Expected one retained graphics configuration')
    source_config = sources[0]
    xml = ET.parse(source_config).getroot()
    if xml.tag != 'UserConfig' or xml.get('Version') != '52':
        raise RuntimeError('Unrecognized graphics-settings schema')
    settings, selections = xml.find('settings'), xml.find('selections')
    for key, value in {'ResolutionWidth':'3472','ResolutionHeight':'2266','Fullscreen':'1','UseDynamicOptimization':'0','PresentInterval':'1'}.items():
        node = settings.find(key)
        if node is None: raise RuntimeError('Missing setting '+key)
        node.set('value', value)
    for key,value in {'UseDynamicOptimization':'0','DLSSMode':'1','DLSSGMode':'2','FSR3Mode':'0','XeSSMode':'0'}.items():
        nodes = [n for n in selections if n.get('id') == key]
        if len(nodes) != 1: raise RuntimeError('Missing/ambiguous option '+key)
        nodes[0].set('value',value)
    ET.indent(xml,space='  ')
    config_bytes = ET.tostring(xml,encoding='utf-8')+b'\n'
    user = next(p for p in (prefix/'drive_c/users').iterdir() if p.name not in ('Public','crossover') and (p/'AppData').is_dir())
    target_config = user/CONFIG_SUFFIX
    previous_receipt = json.loads((root/'fh6-profile.json').read_text()) if (root/'fh6-profile.json').exists() else None
    if target_config.exists() and not previous_receipt:
        raise RuntimeError('Existing Store graphics settings need review before first configuration')
    font = next((p for p in [Path('/Library/Fonts/Arial Unicode.ttf'),Path('/System/Library/Fonts/Supplemental/Arial Unicode.ttf')] if p.is_file()),None)
    if not font: raise RuntimeError('Existing local CJK font missing')
    fontlink = prefix/'drive_c/windows/Fonts/arial-unicode-local.ttf'
    if fontlink.exists() and fontlink.resolve()!=font.resolve(): raise RuntimeError('Unknown existing font')
    message_font = bytearray(92); message_font[:4]=(-12).to_bytes(4,'little',signed=True);message_font[16:20]=(400).to_bytes(4,'little');message_font[23]=1
    message_font[28:]='Arial Unicode MS'.encode('utf-16-le').ljust(64,b'\0')
    reg = ('Windows Registry Editor Version 5.00\n\n'
           '[HKEY_CURRENT_USER\\Software\\Wine\\Mac Driver]\n"RetinaMode"="y"\n"AllowSetGamma"=dword:00000000\n\n'
           '[HKEY_CURRENT_USER\\Software\\Wine\\DllOverrides]\n"version"="native,builtin"\n"nvapi64"="native,builtin"\n\n'
           '[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts]\n"Arial Unicode MS (TrueType)"="arial-unicode-local.ttf"\n\n'
           '[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes]\n'+
           ''.join('"'+s+'"="Arial Unicode MS"\n' for s in ['MS Shell Dlg','MS Shell Dlg,0','MS Shell Dlg 2','MS Shell Dlg 2,0','Tahoma','Arial','Segoe UI','Microsoft Sans Serif','SimSun','Microsoft YaHei'])+
           '\n[HKEY_CURRENT_USER\\Control Panel\\Desktop\\WindowMetrics]\n"MessageFont"=hex:'+','.join(f'{b:02x}' for b in message_font)+'\n')
    engine = Path(json.loads((data/'local-highball.json').read_text())['engineRoot'])
    audio = engine/'frameworks/libhbaudiobuf.dylib'
    oldenv=json.loads((old/'environment.json').read_text())
    if oldenv.get('DYLD_INSERT_LIBRARIES') != str(audio) or not audio.is_file():
        raise RuntimeError('Expected the existing known audio buffer fix')
    environment={'DYLD_INSERT_LIBRARIES':str(audio),'LANG':'zh_CN.UTF-8','LC_ALL':'zh_CN.UTF-8',
                 'WINEDLLOVERRIDES':'winemenubuilder.exe=d;version=n,b;nvapi64=n,b;concrt140,msvcp140,vcruntime140,vcruntime140_1=n,b;gameoverlayrenderer64,gameoverlayrenderer=d',
                 'WINEDEBUG':'fixme-all','NEXUS_PLAYFAB_ADAPTER':'1'}
    record={'schema':1,'gamePath':str(game),'packageVersion':manifest.find('Identity').get('Version'),
            'resolution':[3472,2266],'upscaler':'FSR 3.1.6, explicit 1.5x Quality render ratio via game DLSS inputs',
            'frameGeneration':'FSR 3.1.6 2x plus GPU-verified visibility/UI candidate; live Store visual result pending',
            'environment':environment,'verifiedFiles':{str(dst):expected for src,(dst,expected) in mappings.items()},
            'gameplayVerified':False,'cloud':'official Microsoft/Nexus auto sync; no Steam saves imported'}
    record['verifiedFiles'][str(game/'forzahorizon6.exe')]=EXE_SHA
    record['verifiedFiles'][str(audio)]=sha(audio)
    print(json.dumps({k:v for k,v in record.items() if k not in ('environment','verifiedFiles')},indent=2))
    if not apply: return
    if 'forzahorizon6.exe' in subprocess.check_output(['/bin/ps','-axo','comm='],text=True).lower():
        raise RuntimeError('Close FH6 before configuring the Store installation')
    for src,(dst,expected) in mappings.items():
        dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists(): shutil.copy2(src,dst)
    (game/'OptiScaler.ini').write_text(stream.getvalue())
    target_config.parent.mkdir(parents=True,exist_ok=True); target_config.write_bytes(config_bytes)
    if not fontlink.exists(): fontlink.symlink_to(font)
    (root/'fh6-profile.reg').write_text(reg,encoding='utf-16')
    # Configuration may be changed by the game; only immutable code is pinned.
    (root/'fh6-profile.json').write_text(json.dumps(record,indent=2)+'\n')
    shutil.copy2(HERE/'runner.mjs',root/'runner.mjs')
    node=shutil.which('node')
    if not node: raise RuntimeError('Native Node.js missing')
    subprocess.run([node,str(root/'runner.mjs'),'configure'],check=True,timeout=120)
    app=home/'Applications/Forza Horizon 6 Xbox.app'
    if app.exists() and not (app/'Contents/Info.plist').exists(): raise RuntimeError('Unknown shortcut bundle')
    (app/'Contents/MacOS').mkdir(parents=True,exist_ok=True);(app/'Contents/Resources').mkdir(exist_ok=True)
    icon=old.parent/'game.icns'
    shutil.copy2(icon,app/'Contents/Resources/game.icns')
    launcher=app/'Contents/MacOS/launch'
    launcher.write_text('#!/bin/sh\ncd '+shlex.quote(str(game))+' || exit 1\nexec '+shlex.quote(node)+' '+shlex.quote(str(root/'runner.mjs'))+' run '+shlex.quote(str(game))+'\n')
    launcher.chmod(0o755)
    with (app/'Contents/Info.plist').open('wb') as f:
        plistlib.dump({'CFBundleIdentifier':'local.macgametoolkit.fh6.store','CFBundleName':'Forza Horizon 6 (Xbox)',
                      'CFBundleDisplayName':'Forza Horizon 6 (Xbox)','CFBundleExecutable':'launch','CFBundleIconFile':'game.icns','CFBundlePackageType':'APPL','LSUIElement':True},f)
    subprocess.run([node,str(root/'runner.mjs'),'check',str(game)],check=True,timeout=120)
    print('Prepared Store-only profile and native app shortcut; gameplay test remains with the user.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,default=Path.home()/'Games/Windows/Xbox/Forza-Horizon-6');p.add_argument('--apply',action='store_true')
    a=p.parse_args();prepare(a.game,a.apply)
