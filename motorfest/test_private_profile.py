"""Non-UI regression tests for app isolation, argument preservation and registry scope."""
import importlib.util, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def load(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
runner=load('private_runner','private-runner.py')
installer=load('private_installer','install-private-profile.py')
wow64=load('wow64_selector','repair-wow64-selector.py')
class ScopeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name);client=root/'client';client.mkdir();(client/'steamclient.dylib').touch()
        self.profile=dict(data=str(root/'compatdata/2698940'),candidate=str(root/'candidate'),engine=str(root/'engine'),nativeClient=str(client))
        self.env=dict(STEAM_COMPAT_APP_ID='2698940',STEAM_COMPAT_DATA_PATH=self.profile['data'],WINEPREFIX='wrong',CX_ROOT='wrong',AUDIO_SETTING='keep')
    def test_arguments_and_environment(self):
        args=['waitforexitandrun','Z:\\a b\\TheCrewMotorfest.exe','arg with spaces','literal;$`']
        command,env=runner.launch_plan(self.profile,args,self.env)
        self.assertEqual(command[2:],args[1:]);self.assertEqual(env['AUDIO_SETTING'],'keep')
        self.assertEqual(env['WINEPREFIX'],str(Path(self.profile['data']).resolve()/'pfx'));self.assertNotIn('CX_ROOT',env)
    def test_factory7_loading_is_opt_in_and_preserves_other_renderers(self):
        _,default=runner.launch_plan(self.profile,['run','game.exe'],self.env)
        self.assertIn('d3d11,d3d12,d3d12core,dxgi=b',default['WINEDLLOVERRIDES'])
        _,fixed=runner.launch_plan(dict(self.profile,factory7DxgiNative=True),['run','game.exe'],self.env)
        self.assertIn('d3d11,d3d12,d3d12core=b;dxgi=n,b',fixed['WINEDLLOVERRIDES'])
        self.assertEqual(fixed['GRAPHICS_BACKEND'],default['GRAPHICS_BACKEND'])
        self.assertEqual(fixed['WINEDLLPATH'],default['WINEDLLPATH'])
    def test_private_icd_scope(self):
        data=Path(self.profile['data']); private=data/'.motorfest-private-20261117'
        private.mkdir(parents=True); icd=private/'MoltenVK_icd.json'; icd.write_text('{}')
        profile=dict(self.profile,moltenvkAddressSpaceIcd=str(icd))
        _,env=runner.launch_plan(profile,['run','game.exe'],self.env)
        self.assertEqual(env['VK_DRIVER_FILES'],str(icd.resolve()))
        self.assertEqual(env['VK_ICD_FILENAMES'],str(icd.resolve()))
        with self.assertRaises(ValueError):
            runner.launch_plan(dict(profile,moltenvkAddressSpaceIcd='/tmp/foreign.json'),['run','game.exe'],self.env)
    def test_foreign_games_rejected(self):
        for app in ('1222680','1846380','1551360',''):
            with self.subTest(app=app),self.assertRaises(ValueError):
                runner.launch_plan(self.profile,['run','game.exe'],dict(self.env,STEAM_COMPAT_APP_ID=app))
        with self.assertRaises(ValueError):runner.launch_plan(self.profile,['run','game.exe'],dict(self.env,STEAM_COMPAT_DATA_PATH='/tmp/2698940'))
    def test_install_script_is_not_registered_as_running_game(self):
        inherited=dict(self.env,SteamAppId='2698940',SteamGameId='2698940')
        _,helper=runner.launch_plan(self.profile,['run','iscriptevaluator.exe','script.vdf'],inherited)
        self.assertNotIn('SteamAppId',helper);self.assertNotIn('SteamGameId',helper)
        self.assertEqual(helper['STEAM_COMPAT_APP_ID'],'2698940')
        _,game=runner.launch_plan(self.profile,['waitforexitandrun','game.exe'],self.env)
        self.assertEqual(game['SteamAppId'],'2698940');self.assertEqual(game['SteamGameId'],'2698940')
    def test_queries_and_unknown_verbs(self):
        self.assertEqual(runner.launch_plan(self.profile,['getcompatpath'],self.env),(None,None))
        with self.assertRaises(ValueError):runner.launch_plan(self.profile,['unknown','game.exe'],self.env)
    def test_wow64_selector_preserves_other_architectures(self):
        x86='[Software\\\\Microsoft\\\\Wow64\\\\x86]\n@="wow64cpu.dll"\n'
        other='[Software\\\\Microsoft\\\\Wow64\\\\amd64]\n@="libarm64ecfex.dll"\n\n[Other]\n"Keep"="wow64cpu.dll"\n'
        fixed=wow64.corrected_registry(x86+'\n'+other)
        self.assertEqual(fixed,x86.replace('wow64cpu.dll','libwow64fex.dll')+'\n'+other)
        self.assertEqual(wow64.corrected_registry(fixed),fixed)
        with self.assertRaises(ValueError):wow64.corrected_registry(other)
        with self.assertRaises(ValueError):wow64.corrected_registry(x86.replace('wow64cpu.dll','unknown.dll'))
    def test_registry_scope_idempotence(self):
        source='WINE REGISTRY Version 2\n\n[Software\\\\Wine\\\\AppDefaults\\\\upc.exe\\\\DllOverrides]\n"d3d11"="native,builtin"\n"other"="native"\n\n[OtherGame]\n"Setting"="Keep"\n'
        actual=installer.builtin_browser_registry(source)
        self.assertIn('[OtherGame]\n"Setting"="Keep"',actual)
        self.assertIn('"other"="native"',actual)
        self.assertEqual(actual.count('"d3d11"="builtin"'),3)
        self.assertEqual(installer.builtin_browser_registry(actual),actual)
    def test_failed_install_does_not_erase_external_runner_changes(self):
        path=Path(self.tmp.name)/'run';original=b'original';ours=b'our hook';external=b'other game installer'
        path.write_bytes(ours)
        installer.restore_owned_runner(path,original,ours)
        self.assertEqual(path.read_bytes(),original)
        path.write_bytes(external)
        with self.assertRaises(RuntimeError):installer.restore_owned_runner(path,original,ours)
        self.assertEqual(path.read_bytes(),external)
        with self.assertRaises(RuntimeError):installer.restore_owned_runner(path,original,None)
        self.assertEqual(path.read_bytes(),external)
if __name__=='__main__':unittest.main()
