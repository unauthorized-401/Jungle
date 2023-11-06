#include "threads/loader.h"

/*
	syscall-entry.S
	: rsp 레지스터에 담겨있던 값(유저 스택 포인터 주소값)을 rbx 레지스터에 임시로 저장
	: rsp 레지스터에 tss 주소값을 가져와 넣음, 즉 이때부터 rsp 레지스터에는 커널 스택 포인터 값이 들어있는 것

	movabs $tss, %r12
	: 커널 스택 포인터를 tss로 찾음
	: 커널을 호출해 커널 스택에 데이터를 넣어주어야 하기 때문에

	movq 4(%r12), %rsp
	: tss 값을 %rsp에 넣어줌, 이를 통해 커널 스택 포인터로 이동할 수 있게 됨
	: 커널 모드로 진입하여 ring0의 권한을 갖고, 작업이 끝나면 sysretq를 리턴함
	: sysretq가 반환하면, syscall_handler()를 호출함
*/

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)
	movq %r12, temp2(%rip)     /* callee saved registers */
	movq %rsp, %rbx            /* Store userland rsp    */
	movabs $tss, %r12
	movq (%r12), %r12
	movq 4(%r12), %rsp         /* Read ring0 rsp from the tss */
	/* Now we are in the kernel stack */
	push $(SEL_UDSEG)      /* if->ss */
	push %rbx              /* if->rsp */
	push %r11              /* if->eflags */
	push $(SEL_UCSEG)      /* if->cs */
	push %rcx              /* if->rip */
	subq $16, %rsp         /* skip error_code, vec_no */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	push %rax
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* skip r11 */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi

check_intr:
	btsq $9, %r11          /* Check whether we recover the interrupt */
	jnb no_sti
	sti                    /* restore interrupt */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* if->rip */
	addq $8, %rsp
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
