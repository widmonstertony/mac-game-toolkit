// Read-only evidence for FH6 Store 483970 descriptor exhaustion.
// Does not change game code, allocation sizes, fences, settings or exceptions.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {
using U = std::uintptr_t;
static U base;
static HANDLE logFile = INVALID_HANDLE_VALUE;
static volatile LONG faultReady;
static CONTEXT faultContext;
static DWORD faultThread;
static ULONGLONG faultTick;

bool read(U address, void *output, SIZE_T count) {
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address),
                                       output, count, &got) && got == count;
}
template<class T> bool read(U address, T &value) { return read(address, &value, sizeof(value)); }

LONG CALLBACK fault(EXCEPTION_POINTERS *info) {
    if (info && info->ExceptionRecord && info->ContextRecord &&
        info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ContextRecord->Rip == base + 0x48cf6ef &&
        static_cast<DWORD>(info->ContextRecord->Rdx) == 0x800705aa &&
        InterlockedCompareExchange(&faultReady, -1, 0) == 0) {
        // Only copy the OS-provided exception context. No unwinding, game
        // memory reads, file I/O, allocation or lock acquisition in this callback.
        faultContext = *info->ContextRecord;
        faultThread = GetCurrentThreadId();
        faultTick = GetTickCount64();
        InterlockedExchange(&faultReady, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void append(const char *line, int length) {
    if (length <= 0 || length >= 2048) return;
    DWORD written;
    WriteFile(logFile, line, static_cast<DWORD>(length), &written, nullptr);
}

void snapshot(bool fatal) {
    U manager = 0;
    DWORD batch = 0;
    if (read(base + 0xa9acc48, manager)) read(manager + 0x2d4, batch);
    for (unsigned i = 1; i <= 6; ++i) {
        U descriptor = 0, pool = 0, totals[18] = {}, after[18] = {}, tree[2] = {};
        if (!read(base + 0xa9acbe0 + i * 8, descriptor) ||
            !read(descriptor + 8, pool) || !read(pool, totals) ||
            totals[0] != base + 0x692f258 || !read(pool + 0x908, tree)) continue;
        // Traverse a bounded snapshot without taking game locks. Reject torn
        // snapshots; a counter-stable result still is not an atomic allocation.
        U queue[4096] = {}, visited[4096] = {};
        unsigned head = 0, tail = 0, count = 0;
        U root = 0, freeBytes = 0, largest = 0;
        bool complete = fatal && read(tree[0] + 8, root);
        if (root && root != tree[0]) queue[tail++] = root;
        while (head < tail && complete) {
            U node = queue[head++];
            if (!node || node == tree[0]) continue;
            bool duplicate = false;
            for (unsigned k = 0; k < count; ++k) if (visited[k] == node) duplicate = true;
            if (duplicate || count == 4096) { complete = false; break; }
            visited[count++] = node;
            U fields[6] = {}, range[3] = {};
            if (!read(node, fields) || !read(fields[5], range) ||
                range[0] > totals[7] || range[2] > totals[7] ||
                range[0] + range[2] > totals[7] + 24) { complete = false; break; }
            freeBytes += range[2];
            U pad = (24 - range[0] % 24) % 24;
            U usable = range[2] > pad ? range[2] - pad : 0;
            if (usable > largest) largest = usable;
            for (unsigned k : {0u, 2u}) {
                if (fields[k] && fields[k] != tree[0]) {
                    if (tail == 4096) { complete = false; break; }
                    queue[tail++] = fields[k];
                }
            }
        }
        bool stable = read(pool, after);
        for (unsigned k : {7u, 9u, 14u, 16u, 17u}) stable &= totals[k] == after[k];
        char line[2048];
        int n = std::snprintf(line, sizeof(line),
            "{\"event\":\"descriptor_pool\",\"tick\":%llu,\"fatal\":%u,\"poolIndex\":%u,"
            "\"capacity\":%llu,\"freeLimit\":%llu,\"current\":%llu,\"failures\":%llu,"
            "\"fragmentFailures\":%llu,\"freeTreeBytes\":%llu,\"largestAligned\":%llu,"
            "\"treeNodes\":%llu,\"visited\":%u,\"complete\":%u,\"countersStable\":%u,\"batch\":%lu}\n",
            GetTickCount64(), fatal, i, static_cast<unsigned long long>(totals[7]),
            static_cast<unsigned long long>(totals[9]), static_cast<unsigned long long>(totals[14]),
            static_cast<unsigned long long>(totals[16]), static_cast<unsigned long long>(totals[17]),
            static_cast<unsigned long long>(freeBytes), static_cast<unsigned long long>(largest),
            static_cast<unsigned long long>(tree[1]), count, complete, stable, batch);
        append(line, n);
    }
}

DWORD WINAPI worker(void *) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    base = reinterpret_cast<U>(GetModuleHandleW(nullptr));
    if (!read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
        dos.e_lfanew > 4096 || !read(base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.TimeDateStamp != 0x6aea60eb ||
        nt.OptionalHeader.SizeOfImage != 0xb3e7000) return 0;
    logFile = CreateFileW(L"C:\\highball\\store-plugins\\fh6-descriptor-pool.jsonl", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) return 0;
    if (!AddVectoredExceptionHandler(1, fault)) return 0;
    char line[2048];
    append(line, std::snprintf(line, sizeof(line), "{\"event\":\"descriptor_observer_ready\",\"pid\":%lu,\"tick\":%llu}\n", GetCurrentProcessId(), GetTickCount64()));
    for (unsigned step = 0; step < 144000; ++step) { // Four-hour bound.
        if (InterlockedCompareExchange(&faultReady, 1, 1) == 1) {
            append(line, std::snprintf(line, sizeof(line),
                "{\"event\":\"descriptor_fatal_context\",\"tick\":%llu,\"thread\":%lu,"
                "\"rsi\":\"%llx\",\"rbx\":\"%llx\",\"rdi\":%llu,\"r15\":%llu,\"rsp\":\"%llx\"}\n",
                faultTick, faultThread, faultContext.Rsi, faultContext.Rbx, faultContext.Rdi,
                faultContext.R15, faultContext.Rsp));
            snapshot(true);
            FlushFileBuffers(logFile);
            return 0;
        }
        if (step % 100 == 0) snapshot(false);
        Sleep(100);
    }
    return 0;
}
}

#ifndef FH6_DESCRIPTOR_OBSERVER_FIXTURE
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        HMODULE pinned;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&base), &pinned)) return TRUE;
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
#endif
