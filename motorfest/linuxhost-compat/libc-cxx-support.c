/* SPDX-License-Identifier: AGPL-3.0-only
 * Generic host dependencies used by the unmodified Steam Runtime GNU C++ DSO.
 * Formatting currently supports sequential narrow C conversions. Unsupported
 * positional/wide conversions fail explicitly, never fabricate formatted text.
 */
#define __USE_MINGW_ANSI_STDIO 1
#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elfload.h"
#include "libc.h"

__declspec(noreturn) static void terminal(const char *message)
{
    fputs(message,stderr); fflush(stderr);
    TerminateProcess(GetCurrentProcess(),0xc0000409);
    for (;;) {}
}
static SYSV double lx_ceil(double x) { return ceil(x); }
static SYSV double lx_floor(double x) { return floor(x); }
/* Clang's Windows target still returns fp80 through an implicit pointer with
 * sysv_abi. Linux returns it in st(0), so an explicit adapter is required.
 * Linux input: x at rsp+8, exponent in rdi. MinGW frexpl: result in rcx,
 * pointer to x in rdx, exponent in r8. Preserve the full 80-bit value. */
__asm__(".text\n.globl lx_frexpl\nlx_frexpl:\n"
        "pushq %rbp\nmovq %rsp,%rbp\nsubq $48,%rsp\n"
        "leaq 32(%rsp),%rcx\nleaq 16(%rbp),%rdx\nmovq %rdi,%r8\n"
        "call frexpl\nfldt 32(%rsp)\nmovq %rbp,%rsp\npopq %rbp\nret\n");
extern void lx_frexpl(void);
static SYSV int lx_get_nprocs(void)
{
    DWORD n=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n ? (int)n : 1;
}
static SYSV void lx_assert_fail(const char *assertion,const char *file,unsigned line,const char *function)
{
    fprintf(stderr,"LINUXHOST_ASSERT %s:%u %s: %s\n",file,line,function ? function : "",assertion);
    terminal("LINUXHOST_ASSERT_TERMINATE\n");
}

static int format_error(int error) { *linuxhost_errno()=error; return -1; }
static int read_only_format(const char *s)
{
    uintptr_t p=(uintptr_t)s,end=p+strlen(s)+1;
    if (end<p) return 0;
    while (p<end)
    {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void*)p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
            (m.Protect & (PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return 0;
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;
        if (next<=p) return 0;
        p=next;
    }
    return 1;
}
static int decimal(const char **text,int *value)
{
    const char *p=*text; int v=0;
    while (*p>='0' && *p<='9')
    { if (v>(INT_MAX-(*p-'0'))/10) return 0; v=v*10+*p++-'0'; }
    *value=v; *text=p; return 1;
}

struct lx_va_list { unsigned gp_offset,fp_offset; void *overflow_arg_area,*reg_save_area; };
static uint64_t next_gp(struct lx_va_list *ap)
{
    void *p;
    if(ap->gp_offset<48) { p=(char*)ap->reg_save_area+ap->gp_offset;ap->gp_offset+=8; }
    else { p=ap->overflow_arg_area;ap->overflow_arg_area=(char*)p+8; }
    uint64_t v;memcpy(&v,p,8);return v;
}
static double next_double(struct lx_va_list *ap)
{
    void *p;
    if(ap->fp_offset<176) { p=(char*)ap->reg_save_area+ap->fp_offset;ap->fp_offset+=16; }
    else { p=ap->overflow_arg_area;ap->overflow_arg_area=(char*)p+8; }
    double v;memcpy(&v,p,8);return v;
}
static long double next_long_double(struct lx_va_list *ap)
{
    uintptr_t p=((uintptr_t)ap->overflow_arg_area+15)&~(uintptr_t)15;
    ap->overflow_arg_area=(void*)(p+16);
    long double v;memcpy(&v,(void*)p,sizeof(v));return v;
}
static int format(char *dst,uint64_t size,int flag,const char *fmt,struct lx_va_list *ap)
{
    const char *p=fmt;
    uint64_t total=0;
    if (size && !dst) return format_error(14);
    if (size) dst[0]=0;
    while (*p)
    {
        if (*p!='%')
        {
            if (total+1<size) dst[total]=*p;
            if (++total>INT_MAX) return format_error(75);
            p++; continue;
        }
        ++p;
        if (*p=='%') { if (total+1<size) dst[total]='%'; total++; p++; continue; }
        char flags[8]={0},spec[80]; unsigned nf=0;
        while (*p && strchr("#0- +'",*p))
        {
            /* C locale has no thousands separator. */
            if (*p!='\'' && !strchr(flags,*p)) { if(nf==sizeof(flags)-1)return format_error(22);flags[nf++]=*p; }
            p++;
        }
        int width=0,precision=-1;
        if (*p=='*')
        {
            width=(int)next_gp(ap); p++;
            if (width==INT_MIN) return format_error(75);
            if (width<0) { width=-width; if(!strchr(flags,'-'))flags[nf++]='-'; }
        }
        else if (!decimal(&p,&width)) return format_error(75);
        if (*p=='$') return format_error(22);
        if (*p=='.')
        {
            p++; precision=0;
            if (*p=='*') { precision=(int)next_gp(ap); p++; if(precision<0)precision=-1; }
            else if (!decimal(&p,&precision)) return format_error(75);
        }
        enum { NORMAL,HH,H,L,LL,J,Z,T,LD } length=NORMAL;
        if (*p=='h') { ++p; length=H; if (*p=='h') { length=HH; ++p; } }
        else if (*p=='l') { ++p; length=L; if (*p=='l') { length=LL; ++p; } }
        else if (*p=='j') { ++p; length=J; }
        else if (*p=='z') { ++p; length=Z; }
        else if (*p=='t') { ++p; length=T; }
        else if (*p=='L') { ++p; length=LD; }
        char conversion=*p++;
        if (!conversion) return format_error(22);
        if (conversion=='n')
        {
            if (flag>0 && !read_only_format(fmt)) terminal("LINUXHOST_FORTIFY_REJECT writable_percent_n=true\n");
            void *out=(void*)next_gp(ap);
            if (length==HH) *(signed char*)out=(signed char)total;
            else if (length==H) *(short*)out=(short)total;
            else if (length==NORMAL) *(int*)out=(int)total;
            else if (length!=LD) *(int64_t*)out=total;
            else return format_error(22);
            continue;
        }
        int integer=strchr("diuoxX",conversion)!=NULL;
        int floating=strchr("aAeEfFgG",conversion)!=NULL;
        if ((!integer && !floating && !strchr("scp",conversion)) ||
            ((!integer && !floating) && length!=NORMAL) || (integer && length==LD) ||
            (floating && length!=NORMAL && length!=L && length!=LD)) return format_error(22);
        int at=snprintf(spec,sizeof(spec),"%%%s",flags);
        if(width)at+=snprintf(spec+at,sizeof(spec)-at,"%d",width);
        if(precision>=0)at+=snprintf(spec+at,sizeof(spec)-at,".%d",precision);
        const char *modifier=integer ? "ll" : length==LD ? "L" : "";
        snprintf(spec+at,sizeof(spec)-at,"%s%c",modifier,conversion=='p' ? 's' : conversion);
        uint64_t remaining=total<size ? size-total : 0;
        char *out=remaining ? dst+total : NULL;
        int written;
        if (integer)
        {
            if(conversion=='d' || conversion=='i')
            {
                int64_t n=length>=L ? (int64_t)next_gp(ap) : (int)next_gp(ap);
                if(length==HH)n=(signed char)n; else if(length==H)n=(short)n;
                written=snprintf(out,remaining,spec,(long long)n);
            }
            else
            {
                uint64_t n=length>=L ? (uint64_t)next_gp(ap) : (unsigned)next_gp(ap);
                if(length==HH)n=(unsigned char)n; else if(length==H)n=(unsigned short)n;
                written=snprintf(out,remaining,spec,(unsigned long long)n);
            }
        }
        else if(floating && length==LD)written=snprintf(out,remaining,spec,next_long_double(ap));
        else if(floating)written=snprintf(out,remaining,spec,next_double(ap));
        else if(conversion=='s')written=snprintf(out,remaining,spec,(const char*)next_gp(ap));
        else if(conversion=='c')written=snprintf(out,remaining,spec,(int)next_gp(ap));
        else
        {
            uintptr_t pointer=(uintptr_t)next_gp(ap);char text[24];
            if(pointer)snprintf(text,sizeof(text),"0x%llx",(unsigned long long)pointer); else strcpy(text,"(nil)");
            written=snprintf(out,remaining,spec,text);
        }
        if(written<0)return format_error(22);
        total+=written;
        if(total>INT_MAX)return format_error(75);
    }
    if(size)dst[total<size ? total : size-1]=0;
    return (int)total;
}
SYSV int lx_vsnprintf_chk(char *dst,uint64_t size,int flag,uint64_t object_size,
    const char *fmt,struct lx_va_list *ap)
{
    if(size>object_size)terminal("LINUXHOST_FORTIFY_REJECT before_write=true\n");
    return format(dst,size,flag,fmt,ap);
}
/* Variadic save-area adapter derived from MetalSharp linuxhost libc_io.c,
 * b1ceb881; original AGPL-3.0 provenance retained in README. */
#define VARIADIC(name, core, named, va_reg) \
    __asm__( ".text\n" \
             ".globl " #name "\n" \
             #name ":\n" \
             "    pushq %rbp\n" \
             "    movq %rsp, %rbp\n" \
             "    subq $0xd0, %rsp\n" \
             "    movq %rdi, 0x00(%rsp)\n" \
             "    movq %rsi, 0x08(%rsp)\n" \
             "    movq %rdx, 0x10(%rsp)\n" \
             "    movq %rcx, 0x18(%rsp)\n" \
             "    movq %r8,  0x20(%rsp)\n" \
             "    movq %r9,  0x28(%rsp)\n" \
             "    movaps %xmm0, 0x30(%rsp)\n" \
             "    movaps %xmm1, 0x40(%rsp)\n" \
             "    movaps %xmm2, 0x50(%rsp)\n" \
             "    movaps %xmm3, 0x60(%rsp)\n" \
             "    movaps %xmm4, 0x70(%rsp)\n" \
             "    movaps %xmm5, 0x80(%rsp)\n" \
             "    movaps %xmm6, 0x90(%rsp)\n" \
             "    movaps %xmm7, 0xa0(%rsp)\n" \
             "    movl $" #named "*8, 0xb0(%rsp)\n"  /* gp_offset */ \
             "    movl $48, 0xb4(%rsp)\n"            /* fp_offset */ \
             "    leaq 16(%rbp), %rax\n" \
             "    movq %rax, 0xb8(%rsp)\n"           /* overflow_arg_area */ \
             "    movq %rsp, 0xc0(%rsp)\n"           /* reg_save_area */ \
             "    leaq 0xb0(%rsp), " va_reg "\n" \
             "    call " #core "\n" \
             "    movq %rbp, %rsp\n" \
             "    popq %rbp\n" \
             "    ret\n" )

VARIADIC(lx_snprintf_chk,lx_vsnprintf_chk,5,"%r9");
extern void lx_snprintf_chk(void);
extern int linuxhost_stream_write(void *,const void *,size_t);
SYSV int lx_vfprintf_checked(void *file,int flag,const char *fmt,struct lx_va_list *ap)
{
    struct lx_va_list measuring=*ap,writing=*ap;
    int length=format(NULL,0,flag,fmt,&measuring);
    if(length<0)return -1;
    char *buffer=malloc((size_t)length+1);
    if(!buffer)return format_error(12);
    int actual=format(buffer,(size_t)length+1,flag,fmt,&writing);
    if(actual!=length){free(buffer);return format_error(22);}
    int result=linuxhost_stream_write(file,buffer,(size_t)length);
    free(buffer);return result<0 ? -1 : length;
}
VARIADIC(lx_fprintf_checked,lx_vfprintf_checked,3,"%rcx");
extern void lx_fprintf_checked(void);
#include "scanf-extra.inc"
const struct libc_function linuxhost_cxx_support_functions[]={
    {"ceil",lx_ceil},{"floor",lx_floor},{"frexpl",lx_frexpl},{"get_nprocs",lx_get_nprocs},
    {"__assert_fail",lx_assert_fail},{"__snprintf_chk",lx_snprintf_chk},{"__vsnprintf_chk",lx_vsnprintf_chk},
    {"__fprintf_chk",lx_fprintf_checked},{"sscanf",lx_sscanf_extra},
    {NULL,NULL}
};
