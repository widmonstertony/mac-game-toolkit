// Read-only service query. Does not start, stop, reconfigure or patch BattlEye.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
int main(){
 auto scm=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
 if(!scm){printf("OpenSCManager error=%lu\n",GetLastError());return 1;}
 auto service=OpenServiceW(scm,L"BEService",SERVICE_QUERY_STATUS);
 if(!service){printf("OpenService error=%lu\n",GetLastError());CloseServiceHandle(scm);return 2;}
 SERVICE_STATUS_PROCESS s{};DWORD bytes=0;
 bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&s,sizeof(s),&bytes);
 if(ok)printf("BEService state=%lu win32_exit=%lu service_exit=%lu pid=%lu checkpoint=%lu wait_hint=%lu\n",s.dwCurrentState,s.dwWin32ExitCode,s.dwServiceSpecificExitCode,s.dwProcessId,s.dwCheckPoint,s.dwWaitHint);
 else printf("QueryServiceStatusEx error=%lu\n",GetLastError());
 CloseServiceHandle(service);CloseServiceHandle(scm);return ok?0:3;
}
