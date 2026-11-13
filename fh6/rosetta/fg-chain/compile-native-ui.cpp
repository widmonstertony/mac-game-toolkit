// Build tool only. Never loaded by OptiScaler or copied into the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
int wmain(int argc,wchar_t **argv) {
 if(argc!=3)return 2;
 HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)return 3;
 DWORD bytes=GetFileSize(file,nullptr),read;char *source=(char*)HeapAlloc(GetProcessHeap(),0,bytes);
 if(!source || !ReadFile(file,source,bytes,&read,nullptr) || read!=bytes)return 4;CloseHandle(file);
 ID3DBlob *code=nullptr,*errors=nullptr;
 HRESULT result=D3DCompile(source,bytes,"fh6-native-ui",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(errors){fprintf(stderr,"%s\n",(const char*)errors->GetBufferPointer());errors->Release();}
 HeapFree(GetProcessHeap(),0,source);if(FAILED(result)){printf("compile HRESULT=%08x\n",(unsigned)result);return 5;}
 file=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);if(file==INVALID_HANDLE_VALUE)return 6;
 bool ok=WriteFile(file,code->GetBufferPointer(),(DWORD)code->GetBufferSize(),&read,nullptr)&&read==code->GetBufferSize();CloseHandle(file);
 printf("compiled owned UI shader bytes=%zu\n",code->GetBufferSize());code->Release();return ok?0:7;
}
