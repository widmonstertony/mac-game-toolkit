#pragma once
// Test process only: redirect the candidate's own public NGX imports, reproducing
// the relevant OptiScaler loader/export behavior without loading the whole game.
static HMODULE WINAPI appleProbeLoad(LPCWSTR name){
    if(name&&!_wcsicmp(name,L"nvngx.dll"))return GetModuleHandleW(nullptr);
    return LoadLibraryW(name);
}
static FARPROC WINAPI appleProbeExport(HMODULE module,LPCSTR name){
    if((uintptr_t)name>65535&&!strncmp(name,"NVSDK_NGX_",10))return nullptr;
    return GetProcAddress(module,name);
}
static bool appleProbeRedirect(HMODULE module){
    auto base=(BYTE*)module;auto dos=(IMAGE_DOS_HEADER*)base;
    auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    unsigned replaced=0;
    for(;desc->Name;++desc){
        if(!desc->OriginalFirstThunk)continue;
        auto names=(IMAGE_THUNK_DATA64*)(base+desc->OriginalFirstThunk);
        auto slots=(IMAGE_THUNK_DATA64*)(base+desc->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots){
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            auto name=(const char*)((IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData))->Name;
            void *replacement=nullptr;
            if(!strcmp(name,"LoadLibraryW"))replacement=(void*)appleProbeLoad;
            if(!strcmp(name,"GetProcAddress"))replacement=(void*)appleProbeExport;
            if(!replacement)continue;
            DWORD old=0;if(!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))return false;
            slots->u1.Function=(uintptr_t)replacement;
            DWORD ignored=0;if(!VirtualProtect(&slots->u1.Function,sizeof(void*),old,&ignored))return false;
            ++replaced;
        }
    }
    // New implementations need not import LoadLibraryW at all. GetProcAddress
    // remains imported for ntdll helpers and must have been redirected.
    return replaced!=0;
}
