/* SPDX-License-Identifier: AGPL-3.0-only
 * Owned Linux x64 ELF contract checks, executed through the actual provider DLL.
 */
typedef unsigned long long u64;
struct set {u64 bits[16];};
struct action {void *handler;struct set mask;int flags,pad;void *restorer;};
extern int system(const char*),raise(int),kill(int,int),getpid(void);
extern void *signal(int,void*);
extern int sigaction(int,const struct action*,struct action*),sigemptyset(struct set*),sigaddset(struct set*,int);
extern int sigprocmask(int,const struct set*,struct set*),sigpending(struct set*),sigismember(const struct set*,int);
extern int pthread_sigmask(int,const struct set*,struct set*);
extern int *__errno_location(void);
extern int pthread_create(void**,void*,void *(*)(void*),void*),pthread_join(void*,void**);
static __thread int calls,last;
static void handler(int sig){calls++;last=sig;}
static void *worker(void *unused)
{
    (void)unused;struct set set,old,p;
    if(sigprocmask(2,0,&p)||sigismember(&p,12)!=1)return (void*)5;
    sigemptyset(&set);sigaddset(&set,10);
    if(sigprocmask(0,&set,&old))return (void*)1;
    if(raise(10)||raise(10)||calls)return (void*)2;
    if(sigpending(&p)||sigismember(&p,10)!=1)return (void*)3;
    if(sigprocmask(2,&old,0)||calls!=1||last!=10)return (void*)4;
    return 0;
}
long long signal_test(void)
{
    struct action old={0},act={0},got={0};struct set set,before,after;
    if(sigaction(10,0,&old))return 1;
    if(signal(10,handler)!=old.handler)return 2;
    if(raise(10)||calls!=1||last!=10)return 3;
    if(kill(getpid(),10)||calls!=2)return 4;
    sigemptyset(&set);sigaddset(&set,12);
    if(sigprocmask(0,&set,&before))return 16;
    void *threads[2],*result;
    for(int i=0;i<2;i++)if(pthread_create(&threads[i],0,worker,0))return 5;
    for(int i=0;i<2;i++)if(pthread_join(threads[i],&result)||result)return 6;
    if(calls!=2 || sigprocmask(2,&before,0))return 7;
    sigemptyset(&set);sigaddset(&set,9);sigaddset(&set,19);
    if(sigprocmask(0,&set,&before)||sigprocmask(2,0,&after)||sigismember(&after,9)||sigismember(&after,19))return 8;
    *__errno_location()=123;
    if(pthread_sigmask(99,&set,0)!=22||*__errno_location()!=123)return 9;
    if(signal(9,handler)!=(void*)-1||*__errno_location()!=22)return 10;
    act.handler=handler;act.flags=4; /* SA_SIGINFO must not silently succeed. */
    if(sigaction(10,&act,0)!=-1||*__errno_location()!=95)return 11;
    if(sigaction(10,0,&got)||got.handler!=handler)return 12;
    if(raise(32)!=-1||*__errno_location()!=95)return 13;
    sigemptyset(&set);sigaddset(&set,10);
    if(sigprocmask(0,&set,&before)||raise(10))return 17;
    if(signal(10,(void*)1)!=handler||raise(10)||calls!=2)return 14;
    if(sigprocmask(2,&before,0)||sigpending(&after)||sigismember(&after,10))return 18;
    return sigaction(10,&old,0) ? 15:0;
}
long long signal_default(void){raise(15);return 1;}
static void *process_worker(void *arg)
{
    int status=system(arg ? "exit 19":"exit 27");
    return status==(arg ? 19:27)*256 ? 0:(void*)1;
}
long long process_test(void)
{
    if(system(0)!=1)return 1;
    if(system("exit 7")!=(7<<8))return 2;
    if(system("exit 255")!=(255<<8))return 3;
    if(system("kill -TERM $$")!=15)return 4;
    if(system("kill -USR1 $$")!=10)return 5; /* Darwin 30 -> Linux 10. */
    if(system("exit 0"))return 6;
    if(system("command_that_does_not_exist_linuxhost_913")!=(127<<8))return 7;
    void *threads[2],*result;
    for(int i=0;i<2;i++)if(pthread_create(&threads[i],0,process_worker,(void*)(unsigned long)i))return 8;
    for(int i=0;i<2;i++)if(pthread_join(threads[i],&result)||result)return 9;
    return 0;
}

extern int fork(void),execv(const char*,char *const*),waitpid(int,int*,int);
extern void _exit(int);
extern char *getenv(const char*);
long long wait_test(void)
{
    char *path=getenv("LINUXHOST_WAIT_CHILD");if(!path)return 1;
    char *args[]={path,0};int status=-1;
    int child=fork();if(child<0)return 2;
    if(!child){execv(path,args);_exit(99);}
    if(waitpid(child,&status,1)!=0||status!=-1)return 3;
    if(waitpid(child,&status,0)!=child||status!=(7<<8))return 4;
    if(waitpid(child,&status,0)!=-1||*__errno_location()!=10)return 5;
    child=fork();if(child<0)return 6;
    if(!child){execv(path,args);_exit(99);}
    if(kill(child,9))return 7;
    if(waitpid(child,&status,0)!=child||status!=9)return 8;
    if(waitpid(-1,&status,1)!=-1||*__errno_location()!=10)return 9;
    char *slow_args[]={path,"3000",0},*fast_args[]={path,"20",0};
    int slow=fork();if(slow<0)return 10;
    if(!slow){execv(path,slow_args);_exit(99);}
    int fast=fork();if(fast<0)return 11;
    if(!fast){execv(path,fast_args);_exit(99);}
    if(waitpid(-1,&status,0)!=fast||status!=(7<<8))return 12;
    if(kill(fast,0)!=-1||*__errno_location()!=3)return 13;
    if(kill(slow,9)||waitpid(slow,&status,0)!=slow||status!=9)return 14;
    /* Exceed the original 16-slot lifetime limit. */
    for(int i=0;i<24;i++)
    {
        child=fork();if(child<0)return 15;
        if(!child)_exit(i);
        if(kill(child,0))return 16; /* A zombie exists until reaped. */
        if(waitpid(-1,&status,1)!=child||status!=(i<<8))return 17;
    }
    if(waitpid(-1,&status,1)!=-1||*__errno_location()!=10)return 18;
    return 0;
}
static long direct_call(long nr,long a1,long a2,long a3,long a4)
{
    register long r10 __asm__("r10")=a4;long result;
    __asm__ volatile("syscall":"=a"(result):"a"(nr),"D"(a1),"S"(a2),"d"(a3),"r"(r10):"rcx","r11","memory");
    return result;
}
long long signal_syscall_test(void)
{
    struct kernel_action {void *handler;u64 flags;void *restorer;u64 mask;} action={handler,0,0,0},old={0};
    if(direct_call(13,10,(long)&action,(long)&old,8))return 1;
    u64 block=1UL<<9,previous=0,ready=0;
    *__errno_location()=111;
    if(direct_call(14,0,(long)&block,(long)&previous,16)!=-22||*__errno_location()!=111)return 2;
    if(direct_call(14,0,(long)&block,(long)&previous,8))return 3;
    if(raise(10)||calls)return 4;
    if(direct_call(127,(long)&ready,8,0,0)||ready!=block)return 5;
    if(direct_call(14,2,(long)&previous,0,8)||calls!=1)return 6;
    if(direct_call(13,10,(long)&old,0,8))return 7;
    return 0;
}
