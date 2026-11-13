#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "first-fault.h"
static volatile LONG handled,finished;
static LONG CALLBACK expected(EXCEPTION_POINTERS *p){
 if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || p->ExceptionRecord->NumberParameters!=2 || p->ExceptionRecord->ExceptionInformation[1]!=0x12340000)return EXCEPTION_CONTINUE_SEARCH;
 InterlockedIncrement(&handled);return EXCEPTION_CONTINUE_EXECUTION;
}
static DWORD WINAPI run(void*){
 for(int i=0;i<8;i++) {ULONG_PTR params[2]={1,0x12340000};RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,params);InterlockedIncrement(&finished);}
 return 0;
}
int main(int argc,char **argv){
 if(argc!=2)return 1;wchar_t path[1024];if(!MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,1024))return 2;
 auto handler=AddVectoredExceptionHandler(0,expected);
 if(!handler||!FirstFault::initialize(path))return 3;
 HANDLE threads[8];for(auto &t:threads)t=CreateThread(nullptr,0,run,nullptr,0,nullptr);
 DWORD wait=WaitForMultipleObjects(8,threads,TRUE,10000);
 for(auto t:threads)CloseHandle(t);RemoveVectoredExceptionHandler(handler);
 for(int i=0;i<20 && FirstFault::flushed<FirstFault::writes;i++)Sleep(100);
 printf("wait=%lu handled=%ld continued_execution=%ld records=%ld\n",wait,handled,finished,FirstFault::writes);
 return wait==WAIT_OBJECT_0&&handled==64&&finished==64&&FirstFault::flushed==FirstFault::writes?0:4;
}
