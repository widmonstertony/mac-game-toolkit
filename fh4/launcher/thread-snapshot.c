/* Read-only FH4 process inspection. No injection or debugger attachment.
 * Threads are briefly suspended solely for GetThreadContext, then resumed
 * before any logging or stack read. Only module-relative code addresses print.
 */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
struct module { uintptr_t base; DWORD size; WCHAR name[MAX_PATH]; } modules[512];
static unsigned count;
static DWORD WINAPI test_waiter(void *event){return WaitForSingleObject((HANDLE)event,5000)==WAIT_OBJECT_0?0:1;}
static int code(uintptr_t address){
    for(unsigned i=0;i<count;i++)if(address>=modules[i].base&&address-modules[i].base<modules[i].size){
        printf("%ls+0x%llx",modules[i].name,(unsigned long long)(address-modules[i].base));return 1;
    }
    return 0;
}
int wmain(int argc,WCHAR **argv){
    DWORD pid=0;BOOL self=argc==2&&!wcscmp(argv[1],L"--self-test");
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W p={sizeof(p)};
    if(self)pid=GetCurrentProcessId();
    else for(BOOL ok=Process32FirstW(snap,&p);ok;ok=Process32NextW(snap,&p))
        if(!_wcsicmp(p.szExeFile,L"ForzaHorizon4.exe")){if(pid){puts("AMBIGUOUS_FH4");CloseHandle(snap);return 2;}pid=p.th32ProcessID;}
    CloseHandle(snap);if(!pid){puts("NO_FH4_PROCESS");return 3;}
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,pid);
    if(!process){printf("OPEN_FAILED %lu\n",GetLastError());return 4;}
    snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);MODULEENTRY32W m={sizeof(m)};
    for(BOOL ok=Module32FirstW(snap,&m);ok&&count<512;ok=Module32NextW(snap,&m)){
        modules[count].base=(uintptr_t)m.modBaseAddr;modules[count].size=m.modBaseSize;
        lstrcpynW(modules[count].name,m.szModule,MAX_PATH);count++;
    }
    CloseHandle(snap);if(self){
        printf("SELF_MODULE ");int good=code((uintptr_t)&wmain);puts("");CloseHandle(process);
        HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL),thread=CreateThread(NULL,0,test_waiter,event,0,NULL);
        if(!event||!thread)return 5;
        CONTEXT context={0};context.ContextFlags=CONTEXT_CONTROL;
        DWORD prior=SuspendThread(thread);
        if(prior==(DWORD)-1){SetEvent(event);WaitForSingleObject(thread,5000);return 5;}
        BOOL got=GetThreadContext(thread,&context);DWORD resumed=ResumeThread(thread);
        SetEvent(event);DWORD waited=WaitForSingleObject(thread,5000);
        CloseHandle(thread);CloseHandle(event);
        good=good&&got&&context.Rip&&resumed!=(DWORD)-1&&waited==WAIT_OBJECT_0;
        puts(good?"SNAPSHOT_READ_CONTEXT_RESUME_PASS":"SNAPSHOT_READ_CONTEXT_RESUME_FAIL");return good?0:5;
    }
    snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 t={sizeof(t)};unsigned captured=0;
    for(BOOL ok=Thread32First(snap,&t);ok;ok=Thread32Next(snap,&t)){
        if(t.th32OwnerProcessID!=pid)continue;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,t.th32ThreadID);
        if(!thread)continue;
        CONTEXT c={0};c.ContextFlags=CONTEXT_CONTROL;
        if(SuspendThread(thread)==(DWORD)-1){CloseHandle(thread);continue;}
        BOOL have=GetThreadContext(thread,&c);DWORD error=have?0:GetLastError();
        DWORD resumed=ResumeThread(thread);CloseHandle(thread);
        if(resumed==(DWORD)-1){puts("RESUME_FAILED");CloseHandle(snap);CloseHandle(process);return 6;}
        if(!have){printf("THREAD %lu CONTEXT_FAILED %lu\n",t.th32ThreadID,error);continue;}
        captured++;printf("THREAD %lu PC ",t.th32ThreadID);if(!code((uintptr_t)c.Rip))printf("unmapped");puts("");
        uintptr_t words[128]={0};SIZE_T bytes=0;
        if(ReadProcessMemory(process,(void*)(uintptr_t)c.Rsp,words,sizeof(words),&bytes)){
            for(unsigned i=0;i<bytes/sizeof(uintptr_t);i++){
                // Return candidates, not a proven unwind or arbitrary stack data.
                for(unsigned j=0;j<count;j++)if(words[i]>=modules[j].base&&words[i]-modules[j].base<modules[j].size){
                    printf("  RETURN_CANDIDATE +%u ",i*8);code(words[i]);puts("");break;
                }
            }
        }
    }
    CloseHandle(snap);CloseHandle(process);printf("THREADS_CAPTURED %u\n",captured);return captured?0:7;
}
