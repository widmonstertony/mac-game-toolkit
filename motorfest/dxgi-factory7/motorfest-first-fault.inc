// Opt-in first-fault evidence only; never consumes or changes an exception.
static HANDLE mf_fault_file = INVALID_HANDLE_VALUE;
static PVOID mf_fault_handler;
static LONG mf_fault_recorded;
static LONG CALLBACK mf_first_fault(EXCEPTION_POINTERS *p) {
  if (p->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      InterlockedExchange(&mf_fault_recorded, 1)) return EXCEPTION_CONTINUE_SEARCH;
  struct Evidence { DWORD magic, version, tid, code; ULONGLONG rip, rsp, accessed;
    MEMORY_BASIC_INFORMATION region; BYTE instructions[128]; SIZE_T instructionBytes;
    ULONGLONG stack[256]; SIZE_T stackBytes; } e = {};
  e.magic=0x4d464631; e.version=1; e.tid=GetCurrentThreadId();e.code=p->ExceptionRecord->ExceptionCode;
  e.rip=p->ContextRecord->Rip;e.rsp=p->ContextRecord->Rsp;
  if(p->ExceptionRecord->NumberParameters>1)e.accessed=p->ExceptionRecord->ExceptionInformation[1];
  VirtualQuery((void*)e.rip,&e.region,sizeof(e.region));
  ReadProcessMemory(GetCurrentProcess(),(void*)e.rip,e.instructions,sizeof(e.instructions),&e.instructionBytes);
  ReadProcessMemory(GetCurrentProcess(),(void*)e.rsp,e.stack,sizeof(e.stack),&e.stackBytes);
  DWORD written;WriteFile(mf_fault_file,&e,sizeof(e),&written,nullptr);FlushFileBuffers(mf_fault_file);
  return EXCEPTION_CONTINUE_SEARCH;
}
static void mf_fault_init() {
  WCHAR exe[2048];DWORD en=GetModuleFileNameW(nullptr,exe,2048);
  if(!en||en>=2048)return;
  WCHAR *base=wcsrchr(exe,L'\\');base=base?base+1:exe;
  if(lstrcmpiW(base,L"TheCrewMotorfest.exe"))return;
  WCHAR path[2048];DWORD n=GetEnvironmentVariableW(L"MOTORFEST_FIRST_FAULT_PATH",path,2048);
  if(!n||n>=2048)return;
  mf_fault_file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(mf_fault_file!=INVALID_HANDLE_VALUE)mf_fault_handler=AddVectoredExceptionHandler(1,mf_first_fault);
}
static void mf_fault_shutdown() {
  if(mf_fault_handler)RemoveVectoredExceptionHandler(mf_fault_handler);
  if(mf_fault_file!=INVALID_HANDLE_VALUE)CloseHandle(mf_fault_file);
}
