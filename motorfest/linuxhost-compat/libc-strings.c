/* SPDX-License-Identifier: AGPL-3.0-only
 * Linux x86_64 C-locale/string ABI additions for MetalSharp's Linux host.
 * Ordinary library semantics only; no game or anti-cheat behavior is replaced.
 */
#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include "elfload.h"
#include "libc.h"

static int ascii_lower(int c) { return c>='A' && c<='Z' ? c+32 : c; }
static SYSV int lx_tolower(int c) { return ascii_lower(c); }
static SYSV int lx_isalnum(int c)
{ return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ? 8 : 0; }

static INIT_ONCE case_once=INIT_ONCE_STATIC_INIT;
static int upper_table[384];
static const int *upper_pointer=upper_table+128;
static BOOL CALLBACK init_upper(PINIT_ONCE once,PVOID p,PVOID *context)
{
    for (int c=-128;c<256;c++)
    {
        /* glibc accepts negative signed-char indices for legacy callers; EOF
         * (-1) stays EOF, other negative indices map to their unsigned byte. */
        int v=c< -1 ? c+256 : c;
        upper_table[c+128]=(v>='a' && v<='z') ? v-32 : v;
    }
    return TRUE;
}
static SYSV const int **lx_ctype_toupper_loc(void)
{
    InitOnceExecuteOnce(&case_once,init_upper,NULL,NULL);
    return &upper_pointer;
}
static SYSV int lx_strncasecmp(const char *a,const char *b,uint64_t n)
{
    for (uint64_t i=0;i<n;i++)
    {
        int x=ascii_lower((unsigned char)a[i]), y=ascii_lower((unsigned char)b[i]);
        if (x!=y || !x) return x-y;
    }
    return 0;
}
static SYSV int lx_strcasecmp(const char *a,const char *b)
{ return lx_strncasecmp(a,b,UINT64_MAX); }
static SYSV uint64_t lx_strspn(const char *s,const char *accept)
{ return strspn(s,accept); }

/* Linux long is 64 bits; calling the Windows strtol would truncate to 32 bits.
 * The legacy strtol export accepts bases 2..36 and the C locale. No C23 0b
 * prefix: that is glibc's separate __isoc23_strtol export. */
static SYSV int64_t lx_strtol(const char *s,char **end,int base)
{
    const char *p=s,*digits;
    uint64_t value=0,limit;
    int negative=0,overflow=0;
    if (base && (base<2 || base>36)) { *linuxhost_errno()=22; return 0; }
    while (*p==' ' || (*p>='\t' && *p<='\r')) ++p;
    if (*p=='-' || *p=='+') negative=*p++=='-';
    if ((base==0 || base==16) && p[0]=='0' && (p[1]=='x' || p[1]=='X') &&
        ((p[2]>='0' && p[2]<='9') || (ascii_lower((unsigned char)p[2])>='a' && ascii_lower((unsigned char)p[2])<='f')))
    { base=16; p+=2; }
    if (!base) base=*p=='0' ? 8 : 10;
    digits=p; limit=negative ? (UINT64_C(1)<<63) : INT64_MAX;
    for (;;p++)
    {
        int c=ascii_lower((unsigned char)*p);
        int digit=c>='0' && c<='9' ? c-'0' : c>='a' && c<='z' ? c-'a'+10 : 99;
        if (digit>=base) break;
        if (value>(limit-digit)/(unsigned)base) overflow=1;
        else if (!overflow) value=value*base+digit;
    }
    if (end) *end=(char*)(digits==p ? s : p);
    if (overflow) { *linuxhost_errno()=34; return negative ? INT64_MIN : INT64_MAX; }
    if (negative && value==(UINT64_C(1)<<63)) return INT64_MIN;
    return negative ? -(int64_t)value : (int64_t)value;
}

/* Fail closed before the destination write. The host lacks Linux signal
 * delivery, so this is terminal Windows fail-fast, not SIGABRT emulation. */
__declspec(noreturn) static void bounds_failure(void)
{
    fputs("LINUXHOST_FORTIFY_REJECT before_write=true\n",stderr); fflush(stderr);
    TerminateProcess(GetCurrentProcess(),0xc0000409);
    for (;;) {}
}
static SYSV void *lx_memcpy_chk(void *dst,const void *src,uint64_t count,uint64_t size)
{ if (count>size) bounds_failure(); return memcpy(dst,src,count); }
static SYSV char *lx_stpcpy_chk(char *dst,const char *src,uint64_t size)
{
    uint64_t len=strlen(src);
    if (len>=size) bounds_failure();
    memcpy(dst,src,len+1); return dst+len;
}
static SYSV char *lx_strcpy_chk(char *dst,const char *src,uint64_t size)
{ lx_stpcpy_chk(dst,src,size); return dst; }
static SYSV int64_t lx_fdelt_chk(int64_t fd)
{ if (fd<0 || fd>=1024) bounds_failure(); return fd/64; }

const struct libc_function linuxhost_string_compat_functions[]={
    {"tolower",lx_tolower},{"isalnum",lx_isalnum},{"__ctype_toupper_loc",lx_ctype_toupper_loc},
    {"strcasecmp",lx_strcasecmp},{"strncasecmp",lx_strncasecmp},{"strspn",lx_strspn},{"strtol",lx_strtol},
    {"__memcpy_chk",lx_memcpy_chk},{"__stpcpy_chk",lx_stpcpy_chk},{"__strcpy_chk",lx_strcpy_chk},
    {"__fdelt_chk",lx_fdelt_chk},{NULL,NULL}
};
