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
extern int __snprintf_chk(char *,size_t,int,size_t,const char *,...);
extern int __fprintf_chk(void*,int,const char*,...);
extern double ceil(double),floor(double);
extern long double frexpl(long double,int *);
extern int get_nprocs(void);
extern int strcmp(const char *,const char *);
extern void *mmap(void *,size_t,int,int,int,long);
extern int mprotect(void *,size_t,int),munmap(void *,size_t);
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
    CHECK(ceil(1.25)==2 && floor(-1.25)==-2);
    int exponent=0; CHECK(frexpl(123.5L,&exponent)==0.96484375L && exponent==7);
    CHECK(get_nprocs()>0);
    char formatted[256];
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"%ld %zu %.2f %s",4294967296L, (size_t)17,1.25,"done")==23);
    CHECK(!strcmp(formatted,"4294967296 17 1.25 done"));
    CHECK(__snprintf_chk(formatted,4,0,sizeof(formatted),"%s","abcdef")==6 && !strcmp(formatted,"abc"));
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"[%*.*f]",8,3,1.25)==10 && !strcmp(formatted,"[   1.250]"));
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"%d%d%d%d%d%d%d%d",1,2,3,4,5,6,7,8)==8 && !strcmp(formatted,"12345678"));
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f",1.,2.,3.,4.,5.,6.,7.,8.,9.)==35);
    CHECK(!strcmp(formatted,"1.0 2.0 3.0 4.0 5.0 6.0 7.0 8.0 9.0"));
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"%.5Lf",1.03125L)==7 && !strcmp(formatted,"1.03125"));
    /* Upstream maps its ELF image RWX. A literal there is physically writable,
     * so test the fortified readonly contract with an actually protected page. */
    char *readonly=mmap((void*)0,4096,3,0x22,-1,0);
    CHECK(readonly!=(void*)-1);
    __memcpy_chk(readonly,"abc%n!",7,4096);
    CHECK(mprotect(readonly,4096,1)==0);
    int count=-1; CHECK(__snprintf_chk(formatted,sizeof(formatted),1,sizeof(formatted),readonly,&count)==4 && count==3);
    CHECK(munmap(readonly,4096)==0);
    CHECK(__snprintf_chk(formatted,sizeof(formatted),0,sizeof(formatted),"%2$d",1,2)==-1 && *__errno_location()==22);
    CHECK(__snprintf_chk((void*)0,0,0,0,"%d",1234)==4);
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
    if (kind==5) __snprintf_chk(invalid_destination,8,0,4,"abc");
    if (kind==6) { char format[]="abc%n";int n;char buffer[16]; __snprintf_chk(buffer,16,1,16,format,&n); }
    if (kind==7) { char format[]="abc%n";int n; __fprintf_chk((void*)1,1,format,&n); }
    return -1;
}
