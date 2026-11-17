/* SPDX-License-Identifier: AGPL-3.0-only
 * Checks the engine's logical affinity API; not physical macOS CPU placement. */
typedef unsigned long u64;
extern int sched_getaffinity(int,u64,void*),sched_setaffinity(int,u64,const void*);
extern int *__errno_location(void);
extern int getpid(void),sched_yield(void);
extern u64 pthread_self(void);
extern int pthread_create(u64*,const void*,void*(*)(void*),void*),pthread_join(u64,void**);
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
static long own_thread(void)
{
    u64 original[16]={0},query[16],chosen[16]={0};
    CHECK(sched_getaffinity(0,sizeof(original),original)==0 && original[0]);
    for(unsigned i=0;i<16;i++)query[i]=~0UL;
    CHECK(sched_getaffinity((int)pthread_self(),sizeof(query),query)==0 && query[0]==original[0]);
    for(unsigned i=1;i<16;i++)CHECK(query[i]==0);
    chosen[0]=original[0]&-original[0];
    CHECK(sched_setaffinity(0,sizeof(chosen),chosen)==0);
    for(unsigned i=0;i<16;i++)
    {
        sched_yield();
        CHECK(sched_getaffinity(0,sizeof(query),query)==0 && query[0]==chosen[0]);
    }
    CHECK(sched_setaffinity((int)pthread_self(),sizeof(original),original)==0);
    CHECK(sched_getaffinity(0,sizeof(query),query)==0 && query[0]==original[0]);
    query[0]=123;
    CHECK(sched_getaffinity(0,1,query)==-1 && *__errno_location()==22 && query[0]==123);
    CHECK(sched_getaffinity(0,128,(void*)0)==-1 && *__errno_location()==14);
    CHECK(sched_getaffinity(-1,128,query)==-1 && *__errno_location()==22);
    CHECK(sched_getaffinity(2147483647,128,query)==-1 && *__errno_location()==3);
    CHECK(sched_getaffinity(getpid(),128,query)==-1 && *__errno_location()==95);
    chosen[0]=0;
    CHECK(sched_setaffinity(0,128,chosen)==-1 && *__errno_location()==22);
    CHECK(sched_setaffinity(0,0,(void*)1)==-1 && *__errno_location()==22);
    CHECK(sched_setaffinity(0,128,(void*)0)==-1 && *__errno_location()==14);
    CHECK(sched_getaffinity(0,128,query)==0 && query[0]==original[0]);
    return 0;
}
static void *worker(void *unused){return (void*)own_thread();}
long affinity_test(void)
{
    long result=own_thread();if(result)return result;
    u64 first,second;void *a=(void*)1,*b=(void*)1;
    CHECK(!pthread_create(&first,0,worker,0));
    if(pthread_create(&second,0,worker,0)){pthread_join(first,&a);return __LINE__;}
    CHECK(!pthread_join(first,&a) && !pthread_join(second,&b));
    return a ? (long)a+1000 : b ? (long)b+2000 : 0;
}
