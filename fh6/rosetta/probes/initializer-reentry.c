/* MIT. Fault injection in this project's own Wine ntdll, never in a game or Apple code. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
static BYTE *header_entry, saved_byte;
static volatile LONG header_faults, outer_faults;
__attribute__((naked,noinline)) static void own_ud(void) { __asm__(".byte 0x06; ret"); }
static LONG CALLBACK handler(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    if (e->ContextRecord->Rip == (uintptr_t)header_entry) {
        InterlockedIncrement(&header_faults);
        *header_entry = saved_byte;
        FlushInstructionCache(GetCurrentProcess(), header_entry, 1);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (e->ContextRecord->Rip == (uintptr_t)own_ud) {
        InterlockedIncrement(&outer_faults);
        e->ContextRecord->Rip++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    LONG *configuration = (LONG *)GetProcAddress(ntdll, "__wine_software_exception_chain_configuration");
    header_entry = (BYTE *)GetProcAddress(ntdll, "RtlImageNtHeader");
    if (!configuration || !header_entry) { puts("Requires this project's Wine exception-chain build"); return 2; }
    void *registration = AddVectoredExceptionHandler(1, handler);
    DWORD protection;
    if (!registration || !VirtualProtect(header_entry, 1, PAGE_EXECUTE_READWRITE, &protection)) return 3;
    saved_byte = *header_entry;
    *header_entry = 0x06;
    FlushInstructionCache(GetCurrentProcess(), header_entry, 1);
    /* A cold initializer must allow the original handler to handle its nested fault. */
    InterlockedExchange(configuration, -1);
    puts("Begin cold-policy initializer reentry regression");
    own_ud();
    *header_entry = saved_byte;
    FlushInstructionCache(GetCurrentProcess(), header_entry, 1);
    DWORD ignored;
    VirtualProtect(header_entry, 1, protection, &ignored);
    RemoveVectoredExceptionHandler(registration);
    printf("header_faults=%ld outer_faults=%ld mode=%ld\n", header_faults, outer_faults, configuration[0]);
    return header_faults == 1 && outer_faults == 1 ? 0 : 4;
}
