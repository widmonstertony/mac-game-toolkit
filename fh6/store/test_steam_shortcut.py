import unittest
from pathlib import Path
from steam_shortcut import append_shortcut,entry,string,integer


class ShortcutSafety(unittest.TestCase):
    def test_retains_other_shortcut_exactly_and_is_idempotent(self):
        old=b'\0shortcuts\0\x000\0'+integer('appid',123)+string('AppName','Existing 游戏')+string('Exe','"/Applications/Existing.app"')+b'\0tags\0'+string('0','mine')+b'\x08\x08\x08\x08'
        after,appid,changed=append_shortcut(old,Path('/Users/example/Applications/Forza Horizon 6 Xbox.app'))
        self.assertTrue(changed);self.assertTrue(appid&0x80000000)
        a,b=entry(old,0)['value'][0],entry(after,0)['value'][0]
        self.assertEqual(old[a['start']:a['end']],after[b['start']:b['end']])
        self.assertEqual(append_shortcut(after,Path('/Users/example/Applications/Forza Horizon 6 Xbox.app')),(after,appid,False))

    def test_refuses_unknown_format(self):
        with self.assertRaises(ValueError):append_shortcut(b'\0shortcuts\0\x03unexpected\0\0\0\0\0\x08\x08',Path('/a.app'))


if __name__=='__main__':unittest.main()
