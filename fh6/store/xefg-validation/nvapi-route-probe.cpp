// Isolated loader regression: never injected into the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

static void report(const char* s) {
    DWORD count = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &count, nullptr);
}

extern "C" void entry() {
    const auto opti = LoadLibraryW(L".\\version.dll");
    if (!opti) { report("optiscaler-load=FAIL\n"); ExitProcess(1); }
    report("optiscaler-load=PASS\n");
    ID3D12Device* device = nullptr;
    const HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) { report("device-create=FAIL\n"); ExitProcess(2); }
    const auto nvapi = LoadLibraryW(L"nvapi64.dll");
    report(nvapi == opti ? "nvapi-owner=OptiScaler\n" : "nvapi-owner=external-or-missing\n");
    using Query = void* (__cdecl*)(unsigned int);
    const auto query = reinterpret_cast<Query>(GetProcAddress(nvapi, "nvapi_QueryInterface"));
    if (!query) { report("nvapi-query=FAIL\n"); ExitProcess(3); }
    using Initialize = int (__cdecl*)();
    const auto init = reinterpret_cast<Initialize>(query(0x0150e828));
    if (!init || init() != 0) { report("nvapi-initialize=FAIL\n"); ExitProcess(4); }
    report("nvapi-initialize=PASS\n");
    device->Release();
    ExitProcess(nvapi == opti ? 0 : 5);
}
