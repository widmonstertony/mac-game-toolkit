/* SPDX-License-Identifier: AGPL-3.0-only
 * Linux ELF caller of the candidate's longjmp. The producer uses the documented
 * glibc x86_64 layout, seeds every callee-saved register, and checks them after
 * a nested call has overwritten them. It does not call a host-side setjmp.
 */
typedef unsigned long u64;
struct jump_buffer {u64 registers[8];int mask_saved;int pad;u64 mask[16];};
extern void longjmp(struct jump_buffer*,int) __attribute__((noreturn));
extern long jump_roundtrip(int);
extern int pthread_create(u64*,const void*,void*(*)(void*),void*),pthread_join(u64,void**);
__asm__(".text\n.globl jump_roundtrip\n.type jump_roundtrip,@function\njump_roundtrip:\n"
    "pushq %rbp\npushq %rbx\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
    "subq $216,%rsp\nmovl %edi,208(%rsp)\n"
    "movq $101,%rbx\nmovq $202,%rbp\nmovq $303,%r12\nmovq $404,%r13\n"
    "movq $505,%r14\nmovq $606,%r15\n"
    "movq %rbx,0(%rsp)\nmovq %rbp,%rax\nxorq %fs:48,%rax\nrolq $17,%rax\nmovq %rax,8(%rsp)\n"
    "movq %r12,16(%rsp)\nmovq %r13,24(%rsp)\nmovq %r14,32(%rsp)\nmovq %r15,40(%rsp)\n"
    "movq %rsp,%rax\nxorq %fs:48,%rax\nrolq $17,%rax\nmovq %rax,48(%rsp)\n"
    "leaq 1f(%rip),%rax\nxorq %fs:48,%rax\nrolq $17,%rax\nmovq %rax,56(%rsp)\nmovl $0,64(%rsp)\nmovq %rsp,%rdi\ncall jump_prepare@PLT\n"
    "movq %rsp,%rdi\nmovl 208(%rsp),%esi\ncall jump_deeper\nud2\n"
    "1:\nmovl 208(%rsp),%ecx\ntestl %ecx,%ecx\njne 2f\nmovl $1,%ecx\n2:\n"
    "movl $10,%edx\ncmpl %ecx,%eax\njne 5f\nmovl $11,%edx\ncmpq $101,%rbx\njne 3f\nmovl $12,%edx\ncmpq $202,%rbp\njne 3f\n"
    "movl $13,%edx\ncmpq $303,%r12\njne 3f\nmovl $14,%edx\ncmpq $404,%r13\njne 3f\nmovl $15,%edx\ncmpq $505,%r14\njne 3f\nmovl $16,%edx\ncmpq $606,%r15\njne 3f\n"
    "xorl %eax,%eax\njmp 4f\n5:\naddl $100,%eax\njmp 4f\n3:\nmovl %edx,%eax\n4:\n"
    "addq $216,%rsp\npopq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %rbx\npopq %rbp\nret\n"
    ".size jump_roundtrip,.-jump_roundtrip\n"
    "jump_deeper:\nsubq $120,%rsp\n"
    "movq $-1,%rbx\nmovq $-2,%rbp\nmovq $-3,%r12\nmovq $-4,%r13\nmovq $-5,%r14\nmovq $-6,%r15\n"
    "call jump_choose@PLT\nud2\n");

static long jump_values(void)
{
    u64 guard;
    __asm__ volatile("movq %%fs:48,%0":"=r"(guard));
    if(!guard)return 1; /* Exercise pointer mangling with a nonzero real guard. */
    const int values[]={0,1,42,-7,2147483647,(-2147483647-1)};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)
        if(jump_roundtrip(values[i]))return 10+i;
    return 0;
}

static void *jump_worker(void *argument)
{
    for(unsigned i=0;i<100;i++)
    {
        long result=jump_values();
        if(result)return (void*)result;
    }
    return 0;
}

long jump_test(void)
{
    long result=jump_values();if(result)return result;
    u64 first,second;void *a=(void*)1,*b=(void*)1;
    if(pthread_create(&first,0,jump_worker,0))return 20;
    if(pthread_create(&second,0,jump_worker,0)){pthread_join(first,&a);return 21;}
    if(pthread_join(first,&a) || pthread_join(second,&b))return 22;
    return a || b ? 23 : 0;
}

long jump_mask_rejection(void)
{
    /* Deliberately invalid target pointers must never be restored: the host
     * rejects an invalid saved-mask discriminator before reading target pointers. */
    struct jump_buffer buffer={{0},2,0,{0}};
    longjmp(&buffer,1);
}

extern int sigprocmask(int,const void*,void*);
static __thread int with_mask;
void jump_prepare(struct jump_buffer *buffer)
{
    if(!with_mask)return;
    buffer->mask_saved=1;
    for(int i=0;i<16;i++)buffer->mask[i]=0;
    buffer->mask[0]=1UL<<9;
    u64 empty[16]={0};sigprocmask(2,empty,0);
}
long jump_mask_test(void)
{
    u64 before[16]={0},after[16]={0};
    if(sigprocmask(2,0,before))return 1;
    with_mask=1;long result=jump_roundtrip(37);with_mask=0;
    if(sigprocmask(2,0,after))return 2;
    sigprocmask(2,before,0);
    return result ? 3 : (after[0]!=(1UL<<9) ? 4:0);
}

extern void *signal(int,void*);
static __thread struct jump_buffer *fault_buffer;
static __thread int fault_value,fault_mode,fault_seen;
static void fault_handler(int sig)
{
    fault_seen=sig;
    longjmp(fault_buffer,fault_value);
}
void jump_choose(struct jump_buffer *buffer,int value) __attribute__((noreturn));
void jump_choose(struct jump_buffer *buffer,int value)
{
    if(fault_mode)
    {
        fault_buffer=buffer;fault_value=value;
        if(fault_mode==4)__asm__ volatile("ud2");
        else __asm__ volatile("movq $0,%%rax; movq (%%rax),%%rax":::"rax","memory");
    }
    longjmp(buffer,value);
}
long jump_fault_test(void)
{
    u64 before[16]={0};sigprocmask(2,0,before);
    void *old4=signal(4,fault_handler),*old11=signal(11,fault_handler);
    for(int i=0;i<10;i++)
    {
        fault_seen=0;fault_mode=i%2 ? 11:4;with_mask=1;
        long result=jump_roundtrip(31+i);
        if(result||fault_seen!=fault_mode)return 1000+i*100+result*10+fault_seen;
        with_mask=0;fault_mode=0;
    }
    signal(4,old4);signal(11,old11);sigprocmask(2,before,0);
    return 0;
}
extern void *mmap(void*,unsigned long,int,int,int,long);
extern int mprotect(void*,unsigned long,int),munmap(void*,unsigned long);
static __thread void *protected_page;
static __thread volatile int repaired_fault;
static void repair_page(int sig)
{
    if(sig==11 && !mprotect(protected_page,4096,3))repaired_fault++;
}
long fault_return_test(void)
{
    protected_page=mmap(0,4096,3,0x22,-1,0);
    if(protected_page==(void*)-1)return 1;
    *(volatile unsigned char*)protected_page=73;
    void *old=signal(11,repair_page);
    if(mprotect(protected_page,4096,0))return 2;
    unsigned char value=*(volatile unsigned char*)protected_page;
    u64 fs;__asm__ volatile("rdfsbase %0":"=r"(fs));
    signal(11,old);munmap(protected_page,4096);
    return (value!=73 ? 1:0) | (repaired_fault!=1 ? 2:0) | (!fs ? 4:0);
}
