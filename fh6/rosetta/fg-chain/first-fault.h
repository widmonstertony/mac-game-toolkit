#pragma once
// First-chance is NOT proof of an unhandled exception. In this Highball build
// vectored-continue registration succeeds but callbacks are not dispatched;
// absence of a continue record cannot classify a fault as fatal.
// Exception callbacks only publish bounded metadata. They never handle an
// exception, dereference its faulting pointer, unwind, allocate, format, write
// files or acquire our frame/log locks. A separate worker appends JSON later.
namespace FirstFault {
static HANDLE file=INVALID_HANDLE_VALUE;
static volatile LONG writes,flushed;
struct Site {void *volatile pc;volatile LONG raised,continued;};
struct Record {ULONG64 tick,pc,sp,rcx,rdx,r8,address,operation;DWORD tid,code;LONG occurrence,phase;volatile LONG ready;};
static Site sites[32]{};
static Record records[1024]{};
static char *literal(char *p,const char *s){while(*s)*p++=*s++;return p;}
static char *hex(char *p,ULONG64 n){const char d[]="0123456789abcdef";*p++='"';for(int i=60;i>=0;i-=4)*p++=d[(n>>i)&15];*p++='"';return p;}
static char *field(char *p,const char *key,ULONG64 n){p=literal(p,",\"");p=literal(p,key);p=literal(p,"\":");return hex(p,n);}
static void write(const Record &r){
 char line[768],*p=line;
 p=literal(p,"{\"phase\":\"");p=literal(p,r.phase?"continue":"raise");p=literal(p,"\"");
 p=field(p,"pid",GetCurrentProcessId());p=field(p,"tid",r.tid);p=field(p,"tick",r.tick);
 p=field(p,"code",r.code);p=field(p,"occurrence",r.occurrence);
 p=field(p,"pc",r.pc);p=field(p,"sp",r.sp);p=field(p,"rcx",r.rcx);p=field(p,"rdx",r.rdx);p=field(p,"r8",r.r8);
 p=field(p,"operation",r.operation);p=field(p,"address",r.address);
 p=literal(p,"}\n");DWORD done;WriteFile(file,line,(DWORD)(p-line),&done,nullptr);
}
static DWORD WINAPI writer(void*){
 unsigned next=0;
 for(;;){
  while(next<1024 && InterlockedCompareExchange(&records[next].ready,1,1)){
   write(records[next++]);InterlockedIncrement(&flushed);
  }
  if(next==1024)return 0;Sleep(100);
 }
}
static LONG observe(EXCEPTION_POINTERS *info,bool continued){
 if(!info||!info->ExceptionRecord||!info->ContextRecord||file==INVALID_HANDLE_VALUE||
    info->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
 void *pc=(void*)info->ContextRecord->Rip;
 for(auto &s:sites){
  auto old=InterlockedCompareExchangePointer(&s.pc,pc,nullptr);
  if(old && old!=pc)continue;
  LONG n=InterlockedIncrement(continued?&s.continued:&s.raised);
  // Exponentially spaced recurrences stop intentional AV loops exhausting
  // the file. Exhausted storage only skips observation, never the exception.
  if(n>0 && (n<=4 || (n&(n-1))==0)){
   LONG i=InterlockedIncrement(&writes)-1;
   if(i>=0 && i<1024){auto &r=records[i];auto e=info->ExceptionRecord;auto c=info->ContextRecord;
    r.tick=GetTickCount64();r.tid=GetCurrentThreadId();r.code=e->ExceptionCode;r.phase=continued;r.occurrence=n;
    r.pc=c->Rip;r.sp=c->Rsp;r.rcx=c->Rcx;r.rdx=c->Rdx;r.r8=c->R8;
    r.operation=e->NumberParameters>=2?e->ExceptionInformation[0]:0;r.address=e->NumberParameters>=2?e->ExceptionInformation[1]:0;
    InterlockedExchange(&r.ready,1);
   }
  }
  break;
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
static LONG CALLBACK raised(EXCEPTION_POINTERS *p){return observe(p,false);}
static LONG CALLBACK continued(EXCEPTION_POINTERS *p){return observe(p,true);}
static bool initialize(const wchar_t *path){
 file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return false;
 // Fault callbacks must not be the first touch of the metadata pages.
 ZeroMemory(records,sizeof(records));ZeroMemory(sites,sizeof(sites));
 auto a=AddVectoredExceptionHandler(1,raised);auto b=AddVectoredContinueHandler(1,continued);
 HANDLE worker=a&&b?CreateThread(nullptr,0,writer,nullptr,0,nullptr):nullptr;
 if(!worker){if(a)RemoveVectoredExceptionHandler(a);if(b)RemoveVectoredContinueHandler(b);CloseHandle(file);file=INVALID_HANDLE_VALUE;return false;}
 CloseHandle(worker);
 // The owning ASI is pinned. Process lifetime handle/worker, with no callback
 // removal or shutdown joins under the loader lock. No game settings change.
 return true;
}
}
