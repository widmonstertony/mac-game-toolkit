/* SPDX-License-Identifier: AGPL-3.0-only
 * Headless capability probe only. Does not implement Linux system(), change a
 * game, or execute caller-supplied commands. The only command exits with seven.
 */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};
    startup.cb=sizeof(startup);
    WCHAR command[]=L"\"Z:\\bin\\sh\" -c \"exit 7\"";
    setvbuf(stdout,NULL,_IONBF,0);
    if(!CreateProcessW(L"Z:\\bin\\sh",command,NULL,NULL,FALSE,CREATE_NO_WINDOW,
                       NULL,NULL,&startup,&process))
    {
        printf("NATIVE_SHELL_CREATE_FAILED winerror=%lu\n",GetLastError());
        return 10;
    }
    printf("NATIVE_SHELL_CREATED process_handle=%s thread_handle=%s\n",
           process.hProcess ? "present" : "null",process.hThread ? "present" : "null");
    if(process.hThread)CloseHandle(process.hThread);
    DWORD waited=WaitForSingleObject(process.hProcess,10000);
    if(waited!=WAIT_OBJECT_0)
    {
        /* Only this probe's newly created child, never an existing process. */
        DWORD error=GetLastError();
        if(process.hProcess){TerminateProcess(process.hProcess,124);CloseHandle(process.hProcess);}
        printf("NATIVE_SHELL_WAIT_FAILED wait=%lu winerror=%lu\n",waited,error);return 11;
    }
    DWORD status;
    if(!GetExitCodeProcess(process.hProcess,&status))
    {CloseHandle(process.hProcess);puts("NATIVE_SHELL_STATUS_FAILED");return 12;}
    CloseHandle(process.hProcess);
    printf("NATIVE_SHELL_EXIT status=%lu\n",status);
    return status==7 ? 0 : 13;
}
