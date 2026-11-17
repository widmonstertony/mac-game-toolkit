/* SPDX-License-Identifier: AGPL-3.0-only
 * Synchronous guest signal delivery. Does not install Darwin handlers over Wine.
 * Owned ELF hardware faults use a filtered Wine VEH and restore guest FS.
 * No claim of async host signals, SA_SIGINFO, or altstack support.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libc.h"
#define SYSV __attribute__((sysv_abi))
struct lx_sigset {uint64_t bits[16];};
struct lx_action {void *handler;struct lx_sigset mask;int flags;int pad;void *restorer;};
static SRWLOCK action_lock=SRWLOCK_INIT;
static struct lx_action actions[65];
static __thread uint64_t blocked,pending,guest_fsbase;
void linuxhost_signal_remember_fs(uint64_t base){if(base)guest_fsbase=base;}
static int fail(int e){*linuxhost_errno()=e;return -1;}
static uint64_t bit(int sig){return UINT64_C(1)<<(sig-1);}
static uint64_t allowed(uint64_t mask){return mask & ~((UINT64_C(1)<<8)|(UINT64_C(1)<<18));}
static int valid(int sig){return sig>0 && sig<=64;}
static SYSV int lx_sigemptyset(struct lx_sigset *s){memset(s,0,sizeof(*s));return 0;}
static SYSV int lx_sigfillset(struct lx_sigset *s){memset(s,0,sizeof(*s));s->bits[0]=~UINT64_C(0);return 0;}
static SYSV int lx_sigaddset(struct lx_sigset *s,int sig){if(!valid(sig))return fail(22);s->bits[0]|=bit(sig);return 0;}
static SYSV int lx_sigdelset(struct lx_sigset *s,int sig){if(!valid(sig))return fail(22);s->bits[0]&=~bit(sig);return 0;}
static SYSV int lx_sigismember(const struct lx_sigset *s,int sig){return valid(sig) ? !!(s->bits[0]&bit(sig)):fail(22);}
int linuxhost_signal_enable_faults(void);
static SYSV int lx_sigaction(int sig,const struct lx_action *in,struct lx_action *old)
{
    if(!valid(sig)||sig==9||sig==19)return fail(22);
    struct lx_action value={0};
    if(in)
    {
        value=*in;
        /* Supported: SA_RESTART (no interruptible blocking APIs yet), NODEFER,
         * RESETHAND, glibc's RESTORER marker (no guest rt_sigreturn frame). */
        if((unsigned)value.flags & ~(0x10000000u|0x40000000u|0x80000000u|0x04000000u))return fail(95);
        for(int i=1;i<16;i++)if(value.mask.bits[i])return fail(22);
        if((intptr_t)value.handler==-1)return fail(22);
        value.mask.bits[0]=allowed(value.mask.bits[0]);
    }
    if(in && value.handler && value.handler!=(void*)1 && !linuxhost_signal_enable_faults())return fail(5);
    AcquireSRWLockExclusive(&action_lock);
    if(old)*old=actions[sig];
    if(in)actions[sig]=value;
    if(in && (value.handler==(void*)1 || (!value.handler && (sig==17||sig==23||sig==28))))pending&=~bit(sig);
    ReleaseSRWLockExclusive(&action_lock);
    return 0;
}
static SYSV void *lx_signal_extra(int sig,void *handler)
{
    struct lx_action value={0},old;
    value.handler=handler;value.flags=0x10000000;
    return lx_sigaction(sig,&value,&old) ? (void*)(intptr_t)-1:old.handler;
}
static int deliver(int sig)
{
    struct lx_action action;
    AcquireSRWLockExclusive(&action_lock);action=actions[sig];
    if((unsigned)action.flags&0x80000000u)memset(&actions[sig],0,sizeof(action));
    ReleaseSRWLockExclusive(&action_lock);
    if(action.handler==(void*)1)return 0;
    if(!action.handler)
    {
        if(sig==17||sig==23||sig==28||sig==18)return 0; /* default ignore/continue */
        if(sig==19||sig==20||sig==21||sig==22)return fail(95); /* cannot stop host safely */
        ExitProcess(128+sig);
    }
    uint64_t previous=blocked;
    blocked|=action.mask.bits[0];
    if(!((unsigned)action.flags&0x40000000u))blocked|=bit(sig);
    ((void (SYSV *)(int))action.handler)(sig);
    blocked=previous;
    return 0;
}
uint64_t linuxhost_signal_mask(void){return blocked;}
void linuxhost_signal_inherit(uint64_t mask){blocked=allowed(mask);pending=0;}
static int drain(void)
{
    while(pending&~blocked)
    {
        uint64_t ready=pending&~blocked;int sig=__builtin_ctzll(ready)+1;
        pending&=~bit(sig);if(deliver(sig))return -1;
    }
    return 0;
}
int linuxhost_raise_signal(int sig)
{
    if(!valid(sig))return fail(22);
    /* Realtime signals need queues, not coalescing; report unsupported. */
    if(sig>=32)return fail(95);
    pending|=bit(sig);return drain();
}
static SYSV int lx_sigprocmask(int how,const struct lx_sigset *in,struct lx_sigset *old)
{
    uint64_t value=0;
    if(in)
    {
        if(how<0||how>2)return fail(22);
        for(int i=1;i<16;i++)if(in->bits[i])return fail(22);
        value=allowed(in->bits[0]);
    }
    if(old){memset(old,0,sizeof(*old));old->bits[0]=blocked;}
    if(in){if(how==0)blocked|=value;else if(how==1)blocked&=~value;else blocked=value;}
    return drain();
}
static SYSV int lx_pthread_sigmask(int how,const struct lx_sigset *in,struct lx_sigset *old)
{
    int saved=*linuxhost_errno();int result=lx_sigprocmask(how,in,old);
    int error=result ? *linuxhost_errno():0;*linuxhost_errno()=saved;return error;
}
static SYSV int lx_sigpending(struct lx_sigset *s){memset(s,0,sizeof(*s));s->bits[0]=pending;return 0;}
const struct libc_function linuxhost_signal_extra_functions[]={
    {"signal",lx_signal_extra},{"sigaction",lx_sigaction},{"sigprocmask",lx_sigprocmask},
    {"pthread_sigmask",lx_pthread_sigmask},{"sigpending",lx_sigpending},
    {"sigemptyset",lx_sigemptyset},{"sigfillset",lx_sigfillset},{"sigaddset",lx_sigaddset},
    {"sigdelset",lx_sigdelset},{"sigismember",lx_sigismember},{NULL,NULL}
};
/* Linux x86_64 kernel ABI has an 8-byte mask, unlike libc's 128-byte sigset. */
long long linuxhost_signal_syscall(unsigned long long nr,unsigned long long a1,
    unsigned long long a2,unsigned long long a3,unsigned long long a4)
{
    struct kernel_action {void *handler;uint64_t flags;void *restorer;uint64_t mask;};
    int saved=*linuxhost_errno(),rc=-1,error=0;
    if(nr==13)
    {
        if(a4!=8)return -22;
        struct lx_action in={0},out={0};struct kernel_action *kin=(void*)a2,*kout=(void*)a3;
        if(kin){in.handler=kin->handler;in.flags=(int)kin->flags;in.restorer=kin->restorer;in.mask.bits[0]=kin->mask;}
        if(kin && kin->flags>UINT32_MAX)return -95;
        rc=lx_sigaction((int)a1,kin ? &in:NULL,kout ? &out:NULL);
        if(!rc && kout){kout->handler=out.handler;kout->flags=(unsigned)out.flags;kout->restorer=out.restorer;kout->mask=out.mask.bits[0];}
    }
    else if(nr==14)
    {
        if(a4!=8)return -22;
        struct lx_sigset in={0},out={0};if(a2)in.bits[0]=*(uint64_t*)a2;
        rc=lx_sigprocmask((int)a1,a2 ? &in:NULL,a3 ? &out:NULL);
        if(!rc && a3)*(uint64_t*)a3=out.bits[0];
    }
    else if(nr==127)
    {if(a2!=8)return -22;if(!a1)return -14;*(uint64_t*)a1=pending;return 0;}
    else return -38;
    if(rc)error=*linuxhost_errno();*linuxhost_errno()=saved;return rc ? -error:0;
}
int linuxhost_restore_jump_mask(const void *buffer)
{
    const int *flag=(const int*)((const char*)buffer+64);
    if(*flag==0)return 1;
    if(*flag!=1)return 0;
    return !lx_sigprocmask(2,(const struct lx_sigset*)((const char*)buffer+72),NULL);
}

/* Bridge only faults whose PC belongs to a loaded Linux ELF module. Never
 * intercept faults in game PE modules, Wine, D3DMetal, or native macOS code. */
#include "elfload.h"
static INIT_ONCE fault_once=INIT_ONCE_STATIC_INIT;
static void *fault_registration;
static __thread CONTEXT *fault_context;
static void prepare_fault_return(CONTEXT *context);
static LONG CALLBACK guest_fault(EXCEPTION_POINTERS *exception)
{
    CONTEXT *context=exception->ContextRecord;
    if(!elf_find_address((void*)(uintptr_t)context->Rip))return EXCEPTION_CONTINUE_SEARCH;
    int sig;
    switch(exception->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:sig=11;break;
        case EXCEPTION_IN_PAGE_ERROR:sig=7;break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:case EXCEPTION_PRIV_INSTRUCTION:sig=4;break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:case EXCEPTION_FLT_DIVIDE_BY_ZERO:sig=8;break;
        default:return EXCEPTION_CONTINUE_SEARCH;
    }
    if(fault_context || (blocked&bit(sig)))return EXCEPTION_CONTINUE_SEARCH;
    uint64_t original_fs;
    __asm__ volatile("rdfsbase %0":"=r"(original_fs));
    if(!original_fs && guest_fsbase)__asm__ volatile("wrfsbase %0"::"r"(guest_fsbase));
    CONTEXT *previous=fault_context;fault_context=context;
    int result=deliver(sig);
    fault_context=previous;
    if(!result)prepare_fault_return(context);
    __asm__ volatile("wrfsbase %0"::"r"(original_fs));
    return result ? EXCEPTION_CONTINUE_SEARCH:EXCEPTION_CONTINUE_EXECUTION;
}
static BOOL CALLBACK install_fault_bridge(INIT_ONCE *once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;
    fault_registration=AddVectoredExceptionHandler(1,guest_fault);
    return fault_registration!=NULL;
}
int linuxhost_signal_enable_faults(void)
{
    return InitOnceExecuteOnce(&fault_once,install_fault_bridge,NULL,NULL);
}
/* Wine/FEX clears guest FS both on exception entry and NtContinue. Reinstall
 * the original per-thread TCB after NtContinue, before any ELF instruction. */
static __thread uint64_t jump_resume[9];
uint64_t *linuxhost_get_jump_resume(void)
{
    __asm__ volatile("wrfsbase %0"::"r"(guest_fsbase));
    return jump_resume;
}
extern void linuxhost_fault_resume(void);
__asm__(".text\n.globl linuxhost_fault_resume\nlinuxhost_fault_resume:\n"
    "subq $40,%rsp\ncall linuxhost_get_jump_resume\naddq $40,%rsp\n"
    "movq %rax,%r11\nmovq 0(%r11),%rbx\nmovq 8(%r11),%rbp\n"
    "movq 16(%r11),%r12\nmovq 24(%r11),%r13\nmovq 32(%r11),%r14\nmovq 40(%r11),%r15\n"
    "movq 48(%r11),%rsp\nmovq 56(%r11),%r10\nmovq 64(%r11),%rax\njmp *%r10\n");
void linuxhost_signal_jump_context(const void *buffer,int value)
{
    if(!fault_context)return;

    const uint64_t *r=buffer;uint64_t fsbase;
    __asm__ volatile("rdfsbase %0":"=r"(fsbase));
    if(!fsbase){fputs("GUEST_FAULT_JUMP_NO_FSBASE\n",stderr);fflush(stderr);TerminateProcess(GetCurrentProcess(),0xc0000409);}
    uint64_t guard=*(uint64_t*)(fsbase+48);
    CONTEXT target=*fault_context;fault_context=NULL;
    target.Rbx=r[0];target.Rbp=((r[1]>>17)|(r[1]<<47))^guard;
    target.R12=r[2];target.R13=r[3];target.R14=r[4];target.R15=r[5];
    target.Rsp=((r[6]>>17)|(r[6]<<47))^guard;
    target.Rip=((r[7]>>17)|(r[7]<<47))^guard;
    target.Rax=(uint32_t)(value ? value:1);
    jump_resume[0]=target.Rbx;jump_resume[1]=target.Rbp;jump_resume[2]=target.R12;
    jump_resume[3]=target.R13;jump_resume[4]=target.R14;jump_resume[5]=target.R15;
    jump_resume[6]=target.Rsp;jump_resume[7]=target.Rip;jump_resume[8]=target.Rax;
    target.Rip=(uintptr_t)linuxhost_fault_resume;
    /* Function-entry alignment for the C getter; final RSP restored above. */
    target.Rsp=(target.Rsp&~UINT64_C(15))-8;
    LONG (WINAPI *resume)(CONTEXT*,BOOLEAN)=(void*)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtContinue");
    LONG status=resume ? resume(&target,FALSE):(LONG)0xc0000002;
    fprintf(stderr,"GUEST_FAULT_NTCONTINUE_RETURN status=%08lx pc=%llx flags=%08lx\n",(unsigned long)status,target.Rip,target.ContextFlags);fflush(stderr);
    TerminateProcess(GetCurrentProcess(),0xc0000409);
    for(;;){}
}

void linuxhost_signal_shutdown(void)
{
    if(fault_registration)RemoveVectoredExceptionHandler(fault_registration);
    fault_registration=NULL;
}

/* Normal handler return retries the faulting instruction. Preserve every GPR,
 * flags, FXSAVE state, and the SysV red zone across the FS restoration stub. */
struct __attribute__((aligned(16))) full_guest_resume {
    uint64_t regs[18];unsigned char fp[512];
};
static __thread struct full_guest_resume full_resume;
struct full_guest_resume *linuxhost_get_full_resume(void)
{
    __asm__ volatile("wrfsbase %0"::"r"(guest_fsbase));return &full_resume;
}
extern void linuxhost_fault_return(void);
static void prepare_fault_return(CONTEXT *c)
{
    uint64_t r[]={c->Rax,c->Rcx,c->Rdx,c->Rbx,c->Rsp,c->Rbp,c->Rsi,c->Rdi,
        c->R8,c->R9,c->R10,c->R11,c->R12,c->R13,c->R14,c->R15,c->Rip,c->EFlags};
    memcpy(full_resume.regs,r,sizeof(r));memcpy(full_resume.fp,&c->FltSave,512);
    c->Rip=(uintptr_t)linuxhost_fault_return;c->Rsp=(c->Rsp&~UINT64_C(15))-152;
}
__asm__(".text\n.globl linuxhost_fault_return\nlinuxhost_fault_return:\n"
    "subq $40,%rsp\ncall linuxhost_get_full_resume\naddq $40,%rsp\nmovq %rax,%r11\n"
    "fxrstor64 144(%r11)\npushq 136(%r11)\npopfq\n"
    "movq 0(%r11),%rax\nmovq 8(%r11),%rcx\nmovq 16(%r11),%rdx\nmovq 24(%r11),%rbx\n"
    "movq 40(%r11),%rbp\nmovq 48(%r11),%rsi\nmovq 56(%r11),%rdi\n"
    "movq 64(%r11),%r8\nmovq 72(%r11),%r9\n"
    "movq 96(%r11),%r12\nmovq 104(%r11),%r13\nmovq 112(%r11),%r14\nmovq 120(%r11),%r15\n"
    "movq 32(%r11),%rsp\nmovq 128(%r11),%r10\nmovq %r10,-136(%rsp)\n"
    "movq 80(%r11),%r10\nmovq 88(%r11),%r11\njmp *-136(%rsp)\n");
