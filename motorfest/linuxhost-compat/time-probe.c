/* SPDX-License-Identifier: AGPL-3.0-only */
struct tm {int sec,min,hour,mday,mon,year,wday,yday,isdst,pad;long gmtoff;const char *zone;};
struct timespec {long sec,nsec;};
extern struct tm *localtime_r(const long*,struct tm*);
extern int usleep(unsigned),clock_gettime(int,struct timespec*),strcmp(const char*,const char*);
extern char *getenv(const char*);
extern int puts(const char*);
extern int *__errno_location(void);
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
long compat_test(void)
{
    long instant=1704067200; /* 2024-01-01 00:00 UTC */
    struct tm result;
    CHECK(localtime_r(&instant,&result)==&result);
    CHECK(result.year==123 || result.year==124);
    CHECK(result.mon>=0 && result.mon<12 && result.mday>=1 && result.mday<=31 && result.zone);
    char *tz=getenv("TZ");
    CHECK(tz && (!strcmp(tz,"UTC0") || !strcmp(tz,"PST8PDT")));
    if(!strcmp(tz,"UTC0"))
    {
        CHECK(result.year==124 && result.mon==0 && result.mday==1 && result.hour==0 && result.gmtoff==0 && result.isdst==0);
        puts("TIME_UTC_VALIDATED");
    }
    if(!strcmp(tz,"PST8PDT"))
    {
        CHECK(result.year==123 && result.mon==11 && result.mday==31 && result.hour==16 && result.gmtoff==-28800 && result.isdst==0);
        instant=1719792000; /* 2024-07-01 00:00 UTC */
        CHECK(localtime_r(&instant,&result)==&result);
        CHECK(result.year==124 && result.mon==5 && result.mday==30 && result.hour==17 && result.gmtoff==-25200 && result.isdst==1);
        puts("TIME_PACIFIC_WINTER_SUMMER_VALIDATED");
    }
    instant=9223372036854775807L;result.sec=91;
    CHECK(!localtime_r(&instant,&result) && *__errno_location()==75 && result.sec==91);
    instant=-1;CHECK(!localtime_r(&instant,&result) && *__errno_location()==75);
    CHECK(usleep(0)==0);
    struct timespec before,after;
    CHECK(clock_gettime(1,&before)==0 && usleep(20000)==0 && clock_gettime(1,&after)==0);
    long elapsed=(after.sec-before.sec)*1000000000+after.nsec-before.nsec;
    CHECK(elapsed>=19000000 && elapsed<5000000000L);
    return 0;
}
