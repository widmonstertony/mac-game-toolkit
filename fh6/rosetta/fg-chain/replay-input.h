#pragma once
// Strict readers for private, GPU-fenced saved-frame replay descriptors.
#pragma pack(push,1)
struct Header {UINT64 magic;UINT version,size,w,h,rw,rh,flags,transfer;float ms,scale[2],jitter[2];UINT pitch[7],frameSize,reserved;};
#pragma pack(pop)
static_assert(sizeof(Header)==96,"replay header ABI");
struct Input {Header h{};ChainFrame frame{};BYTE *depth{},*motion{},*scene{};};
static HANDLE open(const wchar_t *dir,const wchar_t *name,DWORD access,DWORD creation){
 wchar_t p[32768];if(wcslen(dir)+wcslen(name)+2>=32768)return INVALID_HANDLE_VALUE;
 wcscpy(p,dir);wcscat(p,L"\\");wcscat(p,name);return CreateFileW(p,access,FILE_SHARE_READ,nullptr,creation,0,nullptr);
}
static bool read(HANDLE f,void *p,DWORD size){DWORD done;return ReadFile(f,p,size,&done,nullptr)&&done==size;}
static BYTE *pixels(const wchar_t *dir,const wchar_t *name,UINT w,UINT h,UINT pitch){
 if(!w||!h||w>16384||h>16384||pitch<w*4||pitch>131072)return nullptr;
 const SIZE_T bytes=SIZE_T(pitch)*h;if(bytes>256*1024*1024)return nullptr;
 HANDLE f=open(dir,name,GENERIC_READ,OPEN_EXISTING);if(f==INVALID_HANDLE_VALUE)return nullptr;
 LARGE_INTEGER length{};if(!GetFileSizeEx(f,&length)||length.QuadPart!=(LONGLONG)bytes){CloseHandle(f);return nullptr;}
 auto raw=(BYTE*)HeapAlloc(GetProcessHeap(),0,bytes),tight=(BYTE*)HeapAlloc(GetProcessHeap(),0,SIZE_T(w)*h*4);
 bool ok=raw&&tight&&read(f,raw,(DWORD)bytes);CloseHandle(f);
 if(ok)for(UINT y=0;y<h;y++)memcpy(tight+SIZE_T(y)*w*4,raw+SIZE_T(y)*pitch,w*4);
 if(raw)HeapFree(GetProcessHeap(),0,raw);if(!ok&&tight){HeapFree(GetProcessHeap(),0,tight);tight=nullptr;}return tight;
}
static bool load(const wchar_t *dir,Input &v,bool compositedInput=false){
 HANDLE f=open(dir,L"replay-input.bin",GENERIC_READ,OPEN_EXISTING);if(f==INVALID_HANDLE_VALUE)return false;
 bool ok=read(f,&v.h,sizeof(v.h))&&read(f,&v.frame,sizeof(v.frame));CloseHandle(f);
 if(!ok||v.h.magic!=0x31594c5052484646ull||v.h.version!=1||v.h.size!=sizeof(v.h)||v.h.frameSize!=sizeof(v.frame)||
    v.h.reserved>1||(v.h.reserved&&!compositedInput))return false;
 v.depth=pixels(dir,L"depth.bin",v.h.rw,v.h.rh,v.h.pitch[0]);
 v.motion=pixels(dir,L"motion.bin",v.h.rw,v.h.rh,v.h.pitch[1]);
 v.scene=compositedInput ? pixels(dir,L"present.bin",v.h.w,v.h.h,v.h.pitch[4])
                         : pixels(dir,L"scene.bin",v.h.w,v.h.h,v.h.pitch[2]);
 return v.depth&&v.motion&&v.scene;
}
