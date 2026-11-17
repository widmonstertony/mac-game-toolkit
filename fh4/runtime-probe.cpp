// Headless baseline check; does not launch FH4 or register a fake UWP package.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <cstdio>
int main(){
 auto init=RoInitialize(RO_INIT_MULTITHREADED);printf("RoInitialize=%08lx\n",(unsigned long)init);if(FAILED(init))return 1;
 const wchar_t *classes[]={L"Windows.Foundation.Uri",L"Windows.ApplicationModel.Core.CoreApplication",L"Windows.UI.Core.CoreWindow",L"Windows.Storage.ApplicationData",L"Windows.ApplicationModel.Package"};
 const IID factoryIID={0x00000035,0,0,{0xc0,0,0,0,0,0,0,0x46}};
 for(auto name:classes){HSTRING id{};WindowsCreateString(name,(UINT32)lstrlenW(name),&id);IUnknown *factory{};
  auto result=RoGetActivationFactory(id,factoryIID,(void**)&factory);printf("class=%ls factory=%08lx\n",name,(unsigned long)result);
  if(factory)factory->Release();WindowsDeleteString(id);}
 // A factory stub is insufficient for UWP startup. Query the actual static
 // interfaces without invoking Run or creating a window.
 const IID appIID={0x0aacf7a4,0x5e1d,0x49df,{0x80,0x34,0xfb,0x6a,0x68,0xbc,0x5e,0xd1}};
 const IID windowIID={0x4d239005,0x3c2a,0x41b1,{0x90,0x22,0x53,0x6b,0xb9,0xcf,0x93,0xb1}};
 const wchar_t *staticNames[]={L"Windows.ApplicationModel.Core.CoreApplication",L"Windows.UI.Core.CoreWindow"};
 const IID *staticIIDs[]={&appIID,&windowIID};
 for(unsigned i=0;i<2;++i){HSTRING id{};IUnknown *iface{};WindowsCreateString(staticNames[i],(UINT32)lstrlenW(staticNames[i]),&id);
  auto result=RoGetActivationFactory(id,*staticIIDs[i],(void**)&iface);printf("class=%ls required_static_interface=%08lx\n",staticNames[i],(unsigned long)result);
  if(iface)iface->Release();WindowsDeleteString(id);}
 const wchar_t *libs[]={L"vccorlib140_app.dll",L"msvcp140_app.dll",L"vcruntime140_app.dll",L"concrt140_app.dll"};
 for(auto name:libs){SetLastError(0);auto h=LoadLibraryW(name);auto error=GetLastError();printf("library=%ls loaded=%u error=%lu\n",name,h!=nullptr,h?0:error);if(h)FreeLibrary(h);}
 RoUninitialize();return 0;
}
