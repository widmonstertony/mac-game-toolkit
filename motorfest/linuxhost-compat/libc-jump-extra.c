/* SPDX-License-Identifier: AGPL-3.0-only
 * Linux x86_64 nonlocal jump restoration. The buffer layout and pointer guard
 * are glibc ABI, not the Windows CRT's jmp_buf. Saved masks use the guest
 * signal state, including jumping from a filtered ELF fault via NtContinue.
 * Darwin signal masks are not changed.
 */
#include <windows.h>
#include <stdio.h>
#include "libc.h"

__declspec(noreturn) void lx_jump_reject(void)
{
    fputs("LINUXHOST_JUMP_UNSUPPORTED_STATE\n",stderr);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(),0xc0000409);
    for(;;){}
}

__asm__(".text\n.globl lx_longjmp_extra\nlx_longjmp_extra:\n"
    /* System V arguments: rdi = glibc buffer, esi = requested result. */
    "pushq %rdi\npushq %rsi\nsubq $40,%rsp\nmovq %rdi,%rcx\n"
    "call linuxhost_restore_jump_mask\ntestl %eax,%eax\nje 3f\n"
    "movq 48(%rsp),%rcx\nmovl 40(%rsp),%edx\ncall linuxhost_signal_jump_context\n"
    "addq $40,%rsp\npopq %rsi\npopq %rdi\njmp 4f\n"
    "3:\naddq $40,%rsp\npopq %rsi\npopq %rdi\njmp 1f\n4:\n"
    "rdfsbase %rcx\ntestq %rcx,%rcx\nje 1f\n"
    "movq 48(%rdi),%r8\nmovq 56(%rdi),%r9\nmovq 8(%rdi),%rdx\n"
    "rorq $17,%r8\nrorq $17,%r9\nrorq $17,%rdx\n"
    "xorq 48(%rcx),%r8\nxorq 48(%rcx),%r9\nxorq 48(%rcx),%rdx\n"
    "movl %esi,%eax\ntestl %eax,%eax\njne 2f\nmovl $1,%eax\n2:\n"
    "movq 0(%rdi),%rbx\nmovq 16(%rdi),%r12\nmovq 24(%rdi),%r13\n"
    "movq 32(%rdi),%r14\nmovq 40(%rdi),%r15\nmovq %rdx,%rbp\n"
    "movq %r8,%rsp\njmp *%r9\n"
    /* Win64 callee shadow space and alignment for the nonreturning reporter. */
    "1:\nsubq $40,%rsp\ncall lx_jump_reject\nud2\n");
extern void lx_longjmp_extra(void);
const struct libc_function linuxhost_jump_extra_functions[]={
    {"longjmp",lx_longjmp_extra},{"siglongjmp",lx_longjmp_extra},{NULL,NULL}
};
