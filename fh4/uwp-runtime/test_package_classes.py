import tempfile
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET
from package_classes import merge_package_classes, APPX, ASM, WINRT

RUNTIME=f'<assembly xmlns="{ASM}"><file name="runtime.dll"><activatableClass xmlns="{WINRT}" name="Windows.UI.Core.CoreWindow" threadingModel="both"/></file></assembly>'


def package(path='owned.dll', cls='Owned.Test', model='both'):
    root=ET.Element('{'+APPX+'}Package')
    ext=ET.SubElement(root,'{'+APPX+'}Extension',Category='windows.activatableClass.inProcessServer')
    server=ET.SubElement(ext,'{'+APPX+'}InProcessServer')
    ET.SubElement(server,'{'+APPX+'}Path').text=path
    ET.SubElement(server,'{'+APPX+'}ActivatableClass',ActivatableClassId=cls,ThreadingModel=model)
    return ET.tostring(root)


class PackageClasses(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.game=Path(self.tmp.name)/'game'; self.game.mkdir()
        (self.game/'owned.dll').write_bytes(b'fixture bytes, never executed')

    def test_preserves_class_identity_and_runtime(self):
        xml,records=merge_package_classes(RUNTIME,package(),self.game)
        classes=list(ET.fromstring(xml).iter('{'+WINRT+'}activatableClass'))
        self.assertEqual([x.get('name') for x in classes],['Windows.UI.Core.CoreWindow','Owned.Test'])
        self.assertEqual(records[0]['classes'],['Owned.Test'])
        self.assertEqual(len(records[0]['sha256']),64)

    def test_rejects_paths_outside_package_and_device_names(self):
        for name in ('../owned.dll','..\\owned.dll','C:\\owned.dll','owned.dll:stream','sub/owned.dll','NUL.dll'):
            with self.subTest(name=name),self.assertRaises(ValueError):
                merge_package_classes(RUNTIME,package(path=name),self.game)

    def test_rejects_symlink_escape(self):
        outside=Path(self.tmp.name)/'outside.dll';outside.write_bytes(b'outside')
        (self.game/'owned.dll').unlink();(self.game/'owned.dll').symlink_to(outside)
        with self.assertRaises(ValueError):merge_package_classes(RUNTIME,package(),self.game)

    def test_rejects_missing_module(self):
        with self.assertRaises(ValueError):merge_package_classes(RUNTIME,package(path='missing.dll'),self.game)

    def test_rejects_overriding_runtime_and_unknown_threading(self):
        for data in (package(cls='Windows.UI.Core.CoreWindow'),package(model='invalid')):
            with self.assertRaises(ValueError):merge_package_classes(RUNTIME,data,self.game)

    def test_rejects_dtd(self):
        with self.assertRaises(ValueError):merge_package_classes(RUNTIME,b'<!DOCTYPE Package>'+package(),self.game)


if __name__=='__main__':unittest.main()
