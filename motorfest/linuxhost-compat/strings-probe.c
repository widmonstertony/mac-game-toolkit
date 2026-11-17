/* SPDX-License-Identifier: AGPL-3.0-only
 * Compiled as Linux ELF (not PE), exercising the real System V import ABI. */
typedef unsigned long size_t;
extern int strcasecmp(const char *,const char *), strncasecmp(const char *,const char *,size_t);
extern size_t strspn(const char *,const char *);
extern long strtol(const char *,char **,int), __fdelt_chk(long);
extern int *__errno_location(void);
extern int tolower(int),isalnum(int);
extern const int **__ctype_toupper_loc(void);
extern void *__memcpy_chk(void *,const void *,size_t,size_t);
extern char *__strcpy_chk(char *,const char *,size_t),*__stpcpy_chk(char *,const char *,size_t);
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)

long compat_test(void)
{
    char b[16],*end; const char *s;
    CHECK(!strcasecmp("AlphaZ","aLPHAz"));
    CHECK(strcasecmp("a","B")<0 && strcasecmp("z","A")>0);
    CHECK(strcasecmp("\xff","\x80")>0);
    CHECK(!strncasecmp((void*)0,(void*)0,0));
    CHECK(!strncasecmp("ABCd","abcE",3) && strncasecmp("ABCd","abcE",4)<0);
    CHECK(strspn("abc123","cba")==3 && strspn("x","")==0);
    CHECK(tolower('G')=='g' && tolower(-1)==-1 && tolower(255)==255);
    CHECK(isalnum('A') && isalnum('5') && !isalnum('_') && !isalnum(255));
    const int *table=*__ctype_toupper_loc();
    CHECK(table['z']=='Z' && table[-1]==-1 && table[-128]==128 && table[255]==255);
    *__errno_location()=7;
    s="  -0x8000000000000000rest";
    CHECK(strtol(s,&end,0)==(-9223372036854775807L-1) && *end=='r' && *__errno_location()==7);
    CHECK(strtol("9223372036854775807",&end,10)==9223372036854775807L && !*end);
    CHECK(strtol("9223372036854775808x",&end,10)==9223372036854775807L && *end=='x' && *__errno_location()==34);
    *__errno_location()=0;
    CHECK(strtol("-9223372036854775809z",&end,10)==(-9223372036854775807L-1) && *end=='z' && *__errno_location()==34);
    CHECK(strtol("077x",&end,0)==63 && *end=='x');
    CHECK(strtol("zz!",&end,36)==1295 && *end=='!');
    s="  +wrong"; CHECK(strtol(s,&end,10)==0 && end==s);
    CHECK(strtol("0x",&end,0)==0 && *end=='x');
    CHECK(strtol("0b11",&end,0)==0 && *end=='b');
    CHECK(strtol("1",&end,1)==0 && *__errno_location()==22);
    CHECK(__memcpy_chk(b,"abc",4,4)==b && b[0]=='a' && b[3]==0);
    CHECK(__stpcpy_chk(b,"word",5)==b+4 && b[4]==0);
    CHECK(__strcpy_chk(b,"ok",3)==b && b[1]=='k' && b[2]==0);
    CHECK(__fdelt_chk(0)==0 && __fdelt_chk(63)==0 && __fdelt_chk(64)==1 && __fdelt_chk(1023)==15);
    return 0;
}

long compat_overflow(long kind)
{
    char *invalid_destination=(char*)1;
    /* Invalid destination proves rejection happens before attempting any write. */
    if (kind==0) __memcpy_chk(invalid_destination,"large",5,4);
    if (kind==1) __stpcpy_chk(invalid_destination,"large",5);
    if (kind==2) __strcpy_chk(invalid_destination,"large",5);
    if (kind==3) __fdelt_chk(1024);
    if (kind==4) __fdelt_chk(-1);
    return -1;
}
