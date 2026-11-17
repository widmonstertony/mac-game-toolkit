/* SPDX-License-Identifier: AGPL-3.0-only
 * Explicit experiment: Linux CPU-set ABI -> Wine's Windows thread-affinity API.
 * These APIs expose the engine's logical masks. On macOS they do not establish
 * physical CPU pinning. Never enable implicitly for an anti-cheat/game session.
 */
#include <windows.h>
#include <processtopologyapi.h>
#include <stdint.h>
#include <string.h>
#include "elfload.h"
#include "libc.h"

static int affinity_error(int error){*linuxhost_errno()=error;return -1;}
static int affinity_win_error(DWORD error)
{
    if(error==ERROR_ACCESS_DENIED)return affinity_error(1);
    if(error==ERROR_NOT_SUPPORTED || error==ERROR_CALL_NOT_IMPLEMENTED)return affinity_error(95);
    if(error==ERROR_INVALID_PARAMETER)return affinity_error(22);
    if(error==ERROR_INVALID_HANDLE)return affinity_error(3);
    return affinity_error(5);
}
static HANDLE affinity_thread(int pid,DWORD rights,int *owned)
{
    *owned=0;
    if(pid<0){affinity_error(22);return NULL;}
    if(!pid || (DWORD)pid==GetCurrentThreadId())return GetCurrentThread();
    /* Linux pid names its leader thread, unlike a Windows process ID. The host
     * does not keep a reliable leader mapping, so do not target a random thread. */
    if((DWORD)pid==GetCurrentProcessId()){affinity_error(95);return NULL;}
    HANDLE thread=OpenThread(rights|THREAD_QUERY_LIMITED_INFORMATION,FALSE,(DWORD)pid);
    if(!thread){affinity_error(GetLastError()==ERROR_ACCESS_DENIED ? 1 : 3);return NULL;}
    DWORD process=GetProcessIdOfThread(thread),status;
    if(process!=GetCurrentProcessId())
    {CloseHandle(thread);affinity_error(process ? 1 : 3);return NULL;}
    if(!GetExitCodeThread(thread,&status) || status!=STILL_ACTIVE)
    {CloseHandle(thread);affinity_error(3);return NULL;}
    *owned=1;return thread;
}

static SYSV int lx_sched_getaffinity(int pid,uint64_t size,void *set)
{
    if(size<sizeof(uint64_t))return affinity_error(22);
    if(!set)return affinity_error(14);
    if(GetActiveProcessorGroupCount()!=1)return affinity_error(95);
    int owned;HANDLE thread=affinity_thread(pid,THREAD_QUERY_INFORMATION,&owned);
    if(!thread)return -1;
    GROUP_AFFINITY group;
    BOOL ok=GetThreadGroupAffinity(thread,&group);DWORD error=GetLastError();
    if(owned)CloseHandle(thread);
    if(!ok)return affinity_win_error(error);
    if(group.Group!=0)return affinity_error(95);
    memset(set,0,(size_t)size);
    uint64_t mask=group.Mask;memcpy(set,&mask,sizeof(mask));
    return 0;
}

static SYSV int lx_sched_setaffinity(int pid,uint64_t size,const void *set)
{
    if(!size)return affinity_error(22);
    if(!set)return affinity_error(14);
    if(GetActiveProcessorGroupCount()!=1)return affinity_error(95);
    uint64_t requested=0;
    memcpy(&requested,set,size<sizeof(requested) ? (size_t)size : sizeof(requested));
    DWORD_PTR process_mask,system_mask;
    if(!GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask))
        return affinity_win_error(GetLastError());
    requested&=process_mask&system_mask;
    if(!requested)return affinity_error(22);
    int owned;HANDLE thread=affinity_thread(pid,THREAD_SET_INFORMATION,&owned);
    if(!thread)return -1;
    DWORD_PTR previous=SetThreadAffinityMask(thread,(DWORD_PTR)requested);DWORD error=GetLastError();
    if(owned)CloseHandle(thread);
    return previous ? 0 : affinity_win_error(error);
}

void *linuxhost_affinity_symbol(const char *name)
{
    int query=!strcmp(name,"sched_getaffinity"),change=!strcmp(name,"sched_setaffinity");
    if(!query && !change)return NULL;
    char mode[16];
    if(GetEnvironmentVariableA("LINUXHOST_AFFINITY_MODE",mode,sizeof(mode))!=7 || strcmp(mode,"windows"))return NULL;
    if(query)return lx_sched_getaffinity;
    return lx_sched_setaffinity;
}
