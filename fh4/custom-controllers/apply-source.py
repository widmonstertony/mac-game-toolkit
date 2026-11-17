#!/usr/bin/env python3
"""Apply only to a private, exact Highball source tree; never a shared engine."""
import argparse
import hashlib
from pathlib import Path
import shutil

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parent
target=a.source/'dlls/windows.gaming.input/manager.c'
raw=target.read_bytes()
if hashlib.sha256(raw).hexdigest()!='afbed1e6fff5ed0fc38c8d2ecea5962a5c19b3dc5ab2f8c77c63cfd8f26dace7':
    raise SystemExit('Source mismatch; refusing an unreviewed manager implementation')
s=raw.decode()
def replace(old,new,count=1):
    global s
    if s.count(old)!=count:raise SystemExit('Source anchor count mismatch: '+old[:90])
    s=s.replace(old,new)
replace('    ICustomGameControllerFactory *factory;\n};','    ICustomGameControllerFactory *factory;\n    BOOL external;\n};')
anchor='static HRESULT WINAPI\nstatics_RegisterCustomFactoryForGipInterface'
replace(anchor,'''static HRESULT cf_register_hardware(ICustomGameControllerFactory *, UINT16, UINT16);
static HRESULT cf_register_gip(ICustomGameControllerFactory *, const GUID *);

'''+anchor)
replace('    FIXME( "iface %p, factory %p, interface_id %s stub!\\n", iface, factory, debugstr_guid(&interface_id) );\n    return E_NOTIMPL;',
        '    return cf_register_gip(factory, &interface_id);')
replace('    FIXME( "iface %p, factory %p, vendor_id %u, product_id %u stub!\\n", iface, factory, vendor_id, product_id );\n    return E_NOTIMPL;',
        '    return cf_register_hardware(factory, vendor_id, product_id);')
replace('    if (!(impl = malloc(sizeof(*impl)))) return E_OUTOFMEMORY;',
        '    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;')
anchor='void manager_on_provider_created( IGameControllerProvider *provider )'
replace(anchor,'''/* External factories implement the public IInspectable contract, not Wine's
 * private IGameControllerImpl Initialize interface. A null result declines a device. */
static HRESULT controller_create_external(ICustomGameControllerFactory *factory,
                                          IGameControllerProvider *provider, struct controller **out)
{
    struct controller *impl;
    HRESULT hr;
    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IGameController_iface.lpVtbl = &controller_vtbl;
    impl->IGameControllerBatteryInfo_iface.lpVtbl = &battery_vtbl;
    impl->ref = 1;
    impl->external = TRUE;
    hr = ICustomGameControllerFactory_CreateGameController(factory, provider, &impl->IInspectable_inner);
    if (FAILED(hr) || !impl->IInspectable_inner)
    {
        if (impl->IInspectable_inner) IInspectable_Release(impl->IInspectable_inner);
        free(impl);
        return FAILED(hr) ? hr : S_FALSE;
    }
    ICustomGameControllerFactory_AddRef((impl->factory = factory));
    IGameControllerProvider_AddRef((impl->provider = provider));
    *out = impl;
    return S_OK;
}

#include "custom-factories.inc"

'''+anchor)
# Register even a provider that lacks Wine's private type projection, without
# invoking any custom factory under manager_cs.
replace('    TRACE( "provider %p\\n", provider );\n\n    if (FAILED(IGameControllerProvider_QueryInterface',
        '    TRACE( "provider %p\\n", provider );\n\n    cf_provider_created(provider, NULL);\n\n    if (FAILED(IGameControllerProvider_QueryInterface')
# The default provider path's callbacks must not re-notify custom controllers.
replace('        controller = LIST_ENTRY( entry, struct controller, entry );\n        hr =',
        '        controller = LIST_ENTRY( entry, struct controller, entry );\n        if (controller->external) { if (next == &controller_list) break; continue; }\n        hr =')
replace('    TRACE( "provider %p\\n", provider );\n\n    EnterCriticalSection( &manager_cs );',
        '    TRACE( "provider %p\\n", provider );\n\n    cf_provider_removed(provider);\n\n    EnterCriticalSection( &manager_cs );')
replace('        if (controller->provider != provider) continue;',
        '        if (controller->external || controller->provider != provider) continue;',2)
target.write_text(s)
shutil.copy2(root/'custom-factories.inc',target.parent/'custom-factories.inc')
print('Patched private manager:',target)
