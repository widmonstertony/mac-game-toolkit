#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int inject_fever_process(unsigned long long dlopen_address, const char *unix_path)
{
    HANDLE snapshot;
    PROCESSENTRY32W process = {0};
    DWORD target_pid = 0;
    HANDLE target = NULL;
    HANDLE thread = NULL;
    void *remote_path = NULL;
    void *remote_code = NULL;
    SIZE_T written;
    DWORD exit_code = 0;
    unsigned char code[] = {
        0x48, 0x89, 0xcf,                         /* mov rdi,rcx */
        0xbe, 0x02, 0x00, 0x00, 0x00,             /* mov esi,RTLD_NOW */
        0x48, 0xb8, 0,0,0,0,0,0,0,0,             /* mov rax,dlopen */
        0x48, 0x83, 0xec, 0x08,                   /* sub rsp,8 */
        0xff, 0xd0,                               /* call rax */
        0x48, 0x83, 0xc4, 0x08,                   /* add rsp,8 */
        0xc3                                      /* ret */
    };

    memcpy(code + 10, &dlopen_address, sizeof(dlopen_address));
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (wcsstr(process.szExeFile, L"FeverGamesInstaller")) {
                target_pid = process.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    if (!target_pid) return 2;

    target = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                         FALSE, target_pid);
    if (!target) return 3;
    remote_path = VirtualAllocEx(target, NULL, strlen(unix_path) + 1,
                                 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    remote_code = VirtualAllocEx(target, NULL, sizeof(code),
                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_path || !remote_code ||
        !WriteProcessMemory(target, remote_path, unix_path, strlen(unix_path) + 1, &written) ||
        !WriteProcessMemory(target, remote_code, code, sizeof(code), &written)) {
        CloseHandle(target);
        return 4;
    }
    FlushInstructionCache(target, remote_code, sizeof(code));
    thread = CreateRemoteThread(target, NULL, 0,
                                (LPTHREAD_START_ROUTINE)remote_code, remote_path, 0, NULL);
    if (!thread) {
        CloseHandle(target);
        return 5;
    }
    WaitForSingleObject(thread, 5000);
    GetExitCodeThread(thread, &exit_code);
    printf("injected pid=%lu dlopen_low32=0x%08lx\n", target_pid, exit_code);
    CloseHandle(thread);
    CloseHandle(target);
    return exit_code ? 0 : 6;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "inject") == 0) {
        unsigned long long address = strtoull(argv[2], NULL, 0);
        return inject_fever_process(address, argv[3]);
    }
    fprintf(stderr, "usage: %s inject DLOPEN_ADDRESS DYLIB_PATH\n", argv[0]);
    return 64;
}
