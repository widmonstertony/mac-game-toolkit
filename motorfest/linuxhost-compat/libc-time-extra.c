/* SPDX-License-Identifier: AGPL-3.0-only */
#include <windows.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include "elfload.h"
#include "libc.h"

struct lx_tm {int sec,min,hour,mday,mon,year,wday,yday,isdst,pad;int64_t gmtoff;const char *zone;};
static __thread char zone_name[256];
static SYSV struct lx_tm *lx_localtime_r(const int64_t *timestamp,struct lx_tm *out)
{
    if(!timestamp || !out){*linuxhost_errno()=22;return NULL;}
    __time64_t value=*timestamp;struct tm native;
    if(value<0 || _localtime64_s(&native,&value))
    { *linuxhost_errno()=75;return NULL; } /* host CRT range, not full glibc range */
    size_t required=0;
    if(_get_tzname(&required,zone_name,sizeof(zone_name),native.tm_isdst>0))
    { *linuxhost_errno()=75;return NULL; }
    int64_t year=(int64_t)native.tm_year+1900,previous=year-1;
    int64_t days=365*(year-1970)+(previous/4-previous/100+previous/400)-(1969/4-1969/100+1969/400)+native.tm_yday;
    struct lx_tm result={native.tm_sec,native.tm_min,native.tm_hour,native.tm_mday,native.tm_mon,
        native.tm_year,native.tm_wday,native.tm_yday,native.tm_isdst,0,
        days*86400+native.tm_hour*3600+native.tm_min*60+native.tm_sec-value,zone_name};
    *out=result;return out;
}

static SYSV int lx_usleep(unsigned usec)
{
    if(!usec){Sleep(0);return 0;}
    HANDLE timer=CreateWaitableTimerW(NULL,TRUE,NULL);
    if(!timer){*linuxhost_errno()=12;return -1;}
    LARGE_INTEGER due;due.QuadPart=-(int64_t)usec*10;
    if(!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE))
    {CloseHandle(timer);*linuxhost_errno()=5;return -1;}
    DWORD waited=WaitForSingleObject(timer,INFINITE);
    CloseHandle(timer);
    if(waited!=WAIT_OBJECT_0){*linuxhost_errno()=5;return -1;}
    return 0;
}
const struct libc_function linuxhost_time_extra_functions[]={
    {"localtime_r",lx_localtime_r},{"usleep",lx_usleep},{NULL,NULL}
};
