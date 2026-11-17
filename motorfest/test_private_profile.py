"""Non-UI regression tests for app isolation, argument preservation and registry scope."""
import importlib.util, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def load(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
runner=load('private_runner','private-runner.py')
installer=load('private_installer','install-private-profile.py')
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
    def test_foreign_games_rejected(self):
        for app in ('1222680','1846380','1551360',''):
            with self.subTest(app=app),self.assertRaises(ValueError):
                runner.launch_plan(self.profile,['run','game.exe'],dict(self.env,STEAM_COMPAT_APP_ID=app))
        with self.assertRaises(ValueError):runner.launch_plan(self.profile,['run','game.exe'],dict(self.env,STEAM_COMPAT_DATA_PATH='/tmp/2698940'))
    def test_queries_and_unknown_verbs(self):
        self.assertEqual(runner.launch_plan(self.profile,['getcompatpath'],self.env),(None,None))
        with self.assertRaises(ValueError):runner.launch_plan(self.profile,['unknown','game.exe'],self.env)
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
