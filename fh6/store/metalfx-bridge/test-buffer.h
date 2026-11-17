#pragma once
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "bridge-abi.h"
struct OwnedBuffer {
    FxBuffer b{};
    OwnedBuffer(uint32_t w,uint32_t h,uint32_t fmt) {
        b.width=w;b.height=h;b.format=fmt;b.rowPitch=(w*(fmt==FX_RGBA16F?8:4)+255)&~255u;
        size_t page=getpagesize();b.bytes=(uint64_t(b.rowPitch)*h+page-1)&~(page-1);
        void *p=nullptr;if(posix_memalign(&p,page,b.bytes))abort();
        b.address=(uint64_t)p;memset(p,0,b.bytes);
    }
    ~OwnedBuffer(){free((void*)b.address);}
    template<class T>T *row(unsigned y){return (T*)(b.address+uint64_t(y)*b.rowPitch);}
};
