#include <inc/mmu.h>
#include <inc/memlayout.h>

// Page fault upcall entrypoint.

// This is where we ask the kernel to redirect us to whenever we cause
// a page fault in user space (see the call to sys_set_pgfault_handler
// in pgfault.c).
//
// When a page fault actually occurs, the kernel switches our ESP to
// point to the user exception stack if we're not already on the user
// exception stack, and then it pushes a UTrapframe onto our user
// exception stack:
//
//	trap-time esp
//	trap-time eflags
//	trap-time eip
//	utf_regs.reg_eax
//	...
//	utf_regs.reg_esi
//	utf_regs.reg_edi
//	utf_err (error code)
//	utf_fault_va            <-- %esp
//
// If this is a recursive fault, the kernel will reserve for us a
// blank word above the trap-time esp for scratch work when we unwind
// the recursive call.
//
// We then have call up to the appropriate page fault handler in C
// code, pointed to by the global variable '_pgfault_handler'.

.text
.globl _pgfault_upcall
_pgfault_upcall:
	// Call the C page fault handler.
	pushl %esp			// function argument: pointer to UTF
	movl _pgfault_handler, %eax
	call *%eax
	addl $4, %esp			// pop function argument

	// use intel syntax since I prefer it
	.intel_syntax noprefix
	
	// Now the C page fault handler has returned and we must revert
	// to the trap time state. This is slightly complicated because *all*
	// registers must look exactly as they did before the page fault, so we
	// need to use a 'ret' instruction to return to the old $pc.

	// Push trap-time %eip onto the trap-time stack
	mov eax, [esp+40] // get eip from UTrapFrame
	mov ebx, [esp+48] // get esp from UTrapFrame
	sub ebx, 4
	mov [ebx], eax
	mov [esp+48], ebx

	// Restore the trap-time registers, except for $esp and eflags
	add esp, 8
	popad

	// Restore eflags from the stack. We cannot use arithmetic instructions
	// after this point!
	add esp, 4
	popfd

	// Switch back to the adjusted trap-time stack.
	pop esp

	// jump back to the instruction that faulted.
	ret

	// switch back to AT&T syntax to avoid breaking anything
	.att_syntax prefix

