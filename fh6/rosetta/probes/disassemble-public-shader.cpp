// Inspect an AMD DXIL input submitted to the public D3D12 PSO API. No driver,
// game executable or protected code is read or modified by this helper.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <dxcapi.h>
#include <cstdio>
int wmain(int argc,wchar_t **argv){
 if(argc!=4)return 1;HMODULE dll=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!dll)return 2;
 auto create=(DxcCreateInstanceProc)GetProcAddress(dll,"DxcCreateInstance");if(!create)return 3;
 IDxcCompiler3 *compiler=nullptr;HRESULT hr=create(CLSID_DxcCompiler,__uuidof(IDxcCompiler3),(void**)&compiler);printf("compiler hr=%08x\n",(unsigned)hr);if(FAILED(hr))return 4;
 HANDLE file=CreateFileW(argv[2],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return 5;
 DWORD size=GetFileSize(file,nullptr),done=0;if(size>1048576)return 6;void *data=HeapAlloc(GetProcessHeap(),0,size);if(!ReadFile(file,data,size,&done,nullptr)||done!=size)return 7;CloseHandle(file);
 DxcBuffer buffer{data,size,0};IDxcResult *result=nullptr;hr=compiler->Disassemble(&buffer,__uuidof(IDxcResult),(void**)&result);printf("disassemble hr=%08x\n",(unsigned)hr);if(FAILED(hr))return 8;
 IDxcBlobUtf8 *text=nullptr;hr=result->GetOutput(DXC_OUT_DISASSEMBLY,__uuidof(IDxcBlobUtf8),(void**)&text,nullptr);if(FAILED(hr))return 9;
 file=CreateFileW(argv[3],GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);if(file==INVALID_HANDLE_VALUE)return 10;
 bool ok=WriteFile(file,text->GetBufferPointer(),(DWORD)text->GetBufferSize(),&done,nullptr);CloseHandle(file);printf("assembly bytes=%llu saved=%d\n",(unsigned long long)text->GetBufferSize(),ok);
 return ok?0:11;
}
