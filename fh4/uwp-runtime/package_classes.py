"""Preserve the owned package's in-process WinRT registrations locally.

This translates declarations, not licensing, and never modifies the package.
"""
import hashlib
from pathlib import Path, PureWindowsPath
import xml.etree.ElementTree as ET

APPX = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
ASM = 'urn:schemas-microsoft-com:asm.v1'
WINRT = 'urn:schemas-microsoft-com:winrt.v1'


def merge_package_classes(runtime_xml, package_xml, game):
    game = Path(game).resolve()
    for raw in (runtime_xml, package_xml):
        raw = raw.encode('utf-8') if isinstance(raw, str) else raw
        if len(raw) > 1024 * 1024 or b'<!DOCTYPE' in raw.upper():
            raise ValueError('Unsupported activation manifest')
    assembly = ET.fromstring(runtime_xml)
    package = ET.fromstring(package_xml)
    if assembly.tag != '{'+ASM+'}assembly' or package.tag != '{'+APPX+'}Package':
        raise ValueError('Unexpected manifest root')
    ids = {e.get('name').casefold() for e in assembly.iter('{'+WINRT+'}activatableClass')}
    files = {e.get('name').casefold(): e for e in assembly.findall('{'+ASM+'}file')}
    records = []
    for ext in package.iter('{'+APPX+'}Extension'):
        if ext.get('Category') != 'windows.activatableClass.inProcessServer':
            continue
        servers = ext.findall('{'+APPX+'}InProcessServer')
        if len(servers) != 1:
            raise ValueError('Expected exactly one in-process server')
        server = servers[0]
        name = (server.findtext('{'+APPX+'}Path') or '').strip()
        path = PureWindowsPath(name)
        # FH4's actual servers are root DLLs. Reject alternate streams, devices,
        # traversal and subdirectories rather than broadening search paths.
        if (not name or path.name != name or ':' in name or '/' in name or
                '\\' in name or path.suffix.lower() != '.dll' or path.is_reserved()):
            raise ValueError('Invalid package server path: '+name)
        dll = (game / name).resolve()
        if dll.parent != game or not dll.is_file():
            raise ValueError('Package server missing or outside package: '+name)
        if name.casefold() in files:
            raise ValueError('Duplicate or overridden server: '+name)
        node = ET.SubElement(assembly, '{'+ASM+'}file', {'name':name})
        files[name.casefold()] = node
        classes = []
        for cls in server.findall('{'+APPX+'}ActivatableClass'):
            class_id, model = cls.get('ActivatableClassId'), cls.get('ThreadingModel')
            if not class_id or class_id.casefold() in ids or model not in ('both','STA','MTA'):
                raise ValueError('Invalid or duplicate package class: '+str(class_id))
            ids.add(class_id.casefold())
            ET.SubElement(node, '{'+WINRT+'}activatableClass', {'name':class_id, 'threadingModel':model})
            classes.append(class_id)
        if not classes:
            raise ValueError('Package server has no classes: '+name)
        records.append({'file':name,'sha256':hashlib.sha256(dll.read_bytes()).hexdigest(), 'classes':classes})
    ET.register_namespace('', ASM)
    ET.register_namespace('winrt', WINRT)
    ET.indent(assembly)
    return ET.tostring(assembly, encoding='unicode', xml_declaration=True), records
