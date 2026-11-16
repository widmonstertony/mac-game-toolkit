#define FH6_DESCRIPTOR_OBSERVER_FIXTURE
#include "descriptor-observer.cpp"
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    base = reinterpret_cast<U>(VirtualAlloc(nullptr, 0xb3e7000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(base);
    U manager[128] = {}, descriptor[16] = {}, pool[512] = {};
    U sentinel[6] = {}, node[6] = {}, range[3] = {24, 0, 4096};
    *reinterpret_cast<U *>(base + 0xa9acc48) = reinterpret_cast<U>(manager);
    *reinterpret_cast<DWORD *>(reinterpret_cast<U>(manager) + 0x2d4) = 512;
    *reinterpret_cast<U *>(base + 0xa9acbe8) = reinterpret_cast<U>(descriptor);
    descriptor[1] = reinterpret_cast<U>(pool);
    pool[0] = base + 0x692f258;
    pool[7] = 8192; pool[9] = 8192; pool[14] = 4096; pool[16] = pool[17] = 1;
    pool[0x908 / 8] = reinterpret_cast<U>(sentinel);
    pool[0x910 / 8] = 1;
    sentinel[1] = reinterpret_cast<U>(node);
    node[0] = node[2] = reinterpret_cast<U>(sentinel);
    node[5] = reinterpret_cast<U>(range);
    U before[512]; std::memcpy(before, pool, sizeof(pool));
    logFile = CreateFileW(L"Z:\\private\\tmp\\fh6-descriptor-observer-fixture.jsonl", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(logFile != INVALID_HANDLE_VALUE);
    snapshot(false);
    snapshot(true);
    CHECK(std::memcmp(before, pool, sizeof(pool)) == 0);
    CHECK(*reinterpret_cast<DWORD *>(reinterpret_cast<U>(manager) + 0x2d4) == 512);
    CONTEXT c{}; EXCEPTION_RECORD e{}; EXCEPTION_POINTERS p{&e, &c};
    CHECK(fault(&p) == EXCEPTION_CONTINUE_SEARCH && faultReady == 0);
    e.ExceptionCode = EXCEPTION_ACCESS_VIOLATION; c.Rip = base + 0x48cf6ef; c.Rdx = 0x800705aa;
    c.Rdi = 512; c.R15 = 0; c.Rsi = reinterpret_cast<U>(manager) + 16;
    CHECK(fault(&p) == EXCEPTION_CONTINUE_SEARCH && faultReady == 1 && faultContext.Rdi == 512);
    c.Rdi = 777;
    CHECK(fault(&p) == EXCEPTION_CONTINUE_SEARCH && faultContext.Rdi == 512);
    SetFilePointer(logFile, 0, nullptr, FILE_BEGIN);
    char output[4096] = {}; DWORD got;
    CHECK(ReadFile(logFile, output, sizeof(output) - 1, &got, nullptr));
    CHECK(std::strstr(output, "\"largestAligned\":4096") && std::strstr(output, "\"countersStable\":1"));
    CHECK(std::strstr(output, "\"complete\":0") && std::strstr(output, "\"complete\":1"));
    CloseHandle(logFile);
    VirtualFree(reinterpret_cast<void *>(base), 0, MEM_RELEASE);
    std::puts("PASS read-only pool snapshot, bounded traversal, fatal context filter and no exception handling");
}
