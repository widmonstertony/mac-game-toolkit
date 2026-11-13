// Read only Wine's saved syscall frame and symbolized stack addresses. No full
// memory dump, debugger attach, register writes or game code modifications.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
struct ThreadBasic {LONG status;void *teb;struct {HANDLE process,thread;} id;ULONG_PTR affinity;LONG priority,base;};
struct SavedFrame {ULONG64 rax,rbx,rcx,rdx,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rip,cs,flags,rsp,ss,rbp,prev,cfa;DWORD syscall,restore;};
static HANDLE process;
static BOOL CALLBACK readRemote(HANDLE, DWORD64 address,void *buffer,DWORD size,DWORD *read){SIZE_T done=0;BOOL r=ReadProcessMemory(process,(void*)address,buffer,size,&done);*read=(DWORD)done;return r;}
static void symbol(ULONG64 address){char storage[sizeof(SYMBOL_INFO)+1024]{};auto s=(SYMBOL_INFO*)storage;s->SizeOfStruct=sizeof(*s);s->MaxNameLen=1024;DWORD64 delta=0;IMAGEHLP_MODULE64 m{};m.SizeOfStruct=sizeof(m);SymGetModuleInfo64(process,address,&m);if(SymFromAddr(process,address,&delta,s))printf("%s!%s+%llx",m.ModuleName,s->Name,(unsigned long long)delta);else printf("%s+%llx",m.ModuleName,(unsigned long long)(address-m.BaseOfImage));}

static bool readExact(ULONG64 at,void *out,SIZE_T bytes){SIZE_T n=0;return ReadProcessMemory(process,(void*)at,out,bytes,&n)&&n==bytes;}
static void exceptionMetadata(const ThreadBasic &basic,const SavedFrame &f){
 DWORD last=0;readExact((ULONG64)basic.teb+0x2c0,&last,4);printf("TEB last exception=%08lx\n",last);
 ULONG64 top=0;if(!readExact((ULONG64)basic.teb+8,&top,8)||top<=f.rsp)return;
 SIZE_T size=(SIZE_T)(top-f.rsp);if(size>1048576)size=1048576;
 printf("Stack scan rsp=%llx top=%llx bytes=%llu\n",f.rsp,top,(unsigned long long)size);
 BYTE *data=(BYTE*)HeapAlloc(GetProcessHeap(),0,size);if(!data)return;
 SIZE_T n=0;ReadProcessMemory(process,(void*)f.rsp,data,size,&n);printf("Stack bytes read=%llu\n",(unsigned long long)n);
 for(SIZE_T i=0;i+sizeof(EXCEPTION_RECORD)<=n;i+=8){EXCEPTION_RECORD record;memcpy(&record,data+i,sizeof(record));
  if(record.ExceptionCode==0xc0000005 && record.NumberParameters==2){IMAGEHLP_MODULE64 mod{};mod.SizeOfStruct=sizeof(mod);SymGetModuleInfo64(process,(ULONG64)record.ExceptionAddress,&mod);printf("Fault module path=%s\n",mod.LoadedImageName);MEMORY_BASIC_INFORMATION memory{};if(VirtualQueryEx(process,(void*)record.ExceptionInformation[1],&memory,sizeof(memory)))printf("Fault memory base=%p size=%llu state=%lx protect=%lx type=%lx\n",memory.BaseAddress,(unsigned long long)memory.RegionSize,memory.State,memory.Protect,memory.Type);printf("Possible access violation record=%llx address=%p operation=%llu fault-address=%llx\n",f.rsp+i,record.ExceptionAddress,record.ExceptionInformation[0],record.ExceptionInformation[1]);}
  if(record.ExceptionCode!=0xe06d7363 || record.NumberParameters!=4 || record.ExceptionInformation[0]<0x19930520 || record.ExceptionInformation[0]>0x19930522)continue;
  printf("C++ candidate at=%llx info=%llx base=%llx\n",f.rsp+i,record.ExceptionInformation[2],record.ExceptionInformation[3]);
  const ULONG64 info=record.ExceptionInformation[2],base=record.ExceptionInformation[3];if(!base || SymGetModuleBase64(process,info)!=base)continue;
  printf("Identified C++ exception record=%llx address=%p module=%llx\n",(unsigned long long)(f.rsp+i),record.ExceptionAddress,(unsigned long long)base);
  DWORD throwInfo[4];if(!readExact(info,throwInfo,sizeof(throwInfo)))continue;
  int count=0;if(!readExact(base+throwInfo[3],&count,4)||count<1||count>16)continue;
  for(int j=0;j<count;j++){DWORD catchRva=0,typeRva=0;if(!readExact(base+throwInfo[3]+4+j*4,&catchRva,4)||!readExact(base+catchRva+4,&typeRva,4))continue;
   char name[128]{};if(!readExact(base+typeRva+16,name,sizeof(name)-1))continue;bool printable=name[0]=='.';for(int k=0;name[k]&&k<127;k++)if((unsigned char)name[k]<32||(unsigned char)name[k]>126)printable=false;
   if(printable){printf("C++ type=%s\n",name);if(!strcmp(name,".H")||!strcmp(name,".J")||!strcmp(name,".K")){DWORD value=0;if(readExact(record.ExceptionInformation[1],&value,4))printf("Thrown numeric error=%08lx\n",value);}}
  }
 }
 HeapFree(GetProcessHeap(),0,data);
}
int main(int argc,char **argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc<3)return 1;DWORD pid=strtoul(argv[1],nullptr,0);process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!process){printf("OpenProcess error=%lu\n",GetLastError());return 2;}
 SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS|SYMOPT_UNDNAME);if(!SymInitialize(process,nullptr,TRUE)){printf("SymInit error=%lu\n",GetLastError());return 3;}
 auto query=(LONG(WINAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*))GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");if(!query)return 4;
 for(int i=2;i<argc;i++){DWORD tid=strtoul(argv[i],nullptr,0);HANDLE th=OpenThread(THREAD_QUERY_INFORMATION,FALSE,tid);ThreadBasic basic{};LONG status=th?query(th,0,&basic,sizeof(basic),nullptr):0xc0000022;printf("\nTID=%04lx query=%08lx teb=%p\n",tid,(ULONG)status,basic.teb);if(status){if(th)CloseHandle(th);continue;}
  ULONG64 saved=0;SIZE_T done=0;if(!ReadProcessMemory(process,(BYTE*)basic.teb+0x378,&saved,8,&done)||done!=8){puts("No saved syscall frame");CloseHandle(th);continue;}SavedFrame f{};if(!ReadProcessMemory(process,(void*)saved,&f,sizeof(f),&done)||done!=sizeof(f)){puts("Cannot read saved syscall frame");CloseHandle(th);continue;}
  printf("Wine saved syscall frame=%llx syscall=%lu rip=%llx rsp=%llx rbp=%llx rcx=%llx\n",(unsigned long long)saved,f.syscall,(unsigned long long)f.rip,(unsigned long long)f.rsp,(unsigned long long)f.rbp,(unsigned long long)f.rcx);
  CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;context.Rip=f.rip;context.Rsp=f.rsp;context.Rbp=f.rbp;context.Rbx=f.rbx;context.Rsi=f.rsi;context.Rdi=f.rdi;context.R12=f.r12;context.R13=f.r13;context.R14=f.r14;context.R15=f.r15;
  STACKFRAME64 frame{};frame.AddrPC.Offset=f.rip;frame.AddrStack.Offset=f.rsp;frame.AddrFrame.Offset=f.rbp;frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
  for(unsigned n=0;n<28;n++){printf("%02u %llx ",n,(unsigned long long)frame.AddrPC.Offset);symbol(frame.AddrPC.Offset);puts("");if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,th,&frame,&context,readRemote,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!frame.AddrPC.Offset)break;}
  exceptionMetadata(basic,f);
  CloseHandle(th);
 }
 HMODULE crt=GetModuleHandleW(L"ucrtbase.dll");if(crt)for(const char *name:{"_execute_onexit_table","exit","_cexit","_c_exit","_exit","abort","_lock","_beginthreadex"}){auto fn=GetProcAddress(crt,name);if(!fn)continue;printf("CRT export %s +%llx\n",name,(unsigned long long)((BYTE*)fn-(BYTE*)crt));}
 SymCleanup(process);CloseHandle(process);return 0;
}
