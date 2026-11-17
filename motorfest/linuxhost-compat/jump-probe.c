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
    "leaq 1f(%rip),%rax\nxorq %fs:48,%rax\nrolq $17,%rax\nmovq %rax,56(%rsp)\nmovl $0,64(%rsp)\n"
    "movq %rsp,%rdi\nmovl 208(%rsp),%esi\ncall jump_deeper\nud2\n"
    "1:\nmovl 208(%rsp),%ecx\ntestl %ecx,%ecx\njne 2f\nmovl $1,%ecx\n2:\n"
    "cmpl %ecx,%eax\njne 3f\ncmpq $101,%rbx\njne 3f\ncmpq $202,%rbp\njne 3f\n"
    "cmpq $303,%r12\njne 3f\ncmpq $404,%r13\njne 3f\ncmpq $505,%r14\njne 3f\ncmpq $606,%r15\njne 3f\n"
    "xorl %eax,%eax\njmp 4f\n3:\nmovl $1,%eax\n4:\n"
    "addq $216,%rsp\npopq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %rbx\npopq %rbp\nret\n"
    ".size jump_roundtrip,.-jump_roundtrip\n"
    "jump_deeper:\nsubq $120,%rsp\n"
    "movq $-1,%rbx\nmovq $-2,%rbp\nmovq $-3,%r12\nmovq $-4,%r13\nmovq $-5,%r14\nmovq $-6,%r15\n"
    "call longjmp@PLT\nud2\n");

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
     * cannot restore this saved signal mask, so rejection must happen first. */
    struct jump_buffer buffer={{0},1,0,{0}};
    longjmp(&buffer,1);
}
