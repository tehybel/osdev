/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC (no error code) for traps where the CPU doesn't push
 * an error code. It pushes a 0 in place of the error code, so the trap frame
 * has the same format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

// used to handle bad traps; just spin in place.
.globl badint
.type badint, @function
badint: jmp badint

// generate entry points for all the different exceptions
// a table showing which of these push error codes can be found here:
// http://wiki.osdev.org/Exceptions

TRAPHANDLER_NOEC(trap_divide, T_DIVIDE ) 	// divide error
TRAPHANDLER_NOEC(trap_debug,  T_DEBUG  ) 	// debug exception
TRAPHANDLER_NOEC(trap_nmi,    T_NMI    ) 	// non-maskable interrupt
TRAPHANDLER_NOEC(trap_brkpt,  T_BRKPT  ) 	// breakpoint
TRAPHANDLER_NOEC(trap_oflow,  T_OFLOW  ) 	// overflow
TRAPHANDLER_NOEC(trap_bound,  T_BOUND  ) 	// bounds check
TRAPHANDLER_NOEC(trap_illop,  T_ILLOP  ) 	// illegal opcode
TRAPHANDLER_NOEC(trap_device, T_DEVICE ) 	// device not available
TRAPHANDLER     (trap_dblflt, T_DBLFLT ) 	// double fault
TRAPHANDLER     (trap_tss,    T_TSS    ) 	// invalid task switch segment
TRAPHANDLER     (trap_segnp,  T_SEGNP  ) 	// segment not present
TRAPHANDLER     (trap_stack,  T_STACK  ) 	// stack exception
TRAPHANDLER     (trap_gpflt,  T_GPFLT  ) 	// general protection fault
TRAPHANDLER     (trap_pgflt,  T_PGFLT  ) 	// page fault
TRAPHANDLER_NOEC(trap_fperr,  T_FPERR  ) 	// floating point error
TRAPHANDLER     (trap_align,  T_ALIGN  ) 	// aligment check
TRAPHANDLER_NOEC(trap_mchk,   T_MCHK   ) 	// machine check
TRAPHANDLER_NOEC(trap_simerr, T_SIMDERR)	// SIMD floating point error

TRAPHANDLER_NOEC(trap_syscall, T_SYSCALL)	// syscalls

// all traps enter the trap() C function via this code
_alltraps:

// make sure the TOS looks like a struct Trapframe
push %ds
push %es
pushal

// load the kernel data segment selector into %ds and %es so we can access
// kernel data
mov $GD_KD, %eax
mov %ax, %ds
mov %ax, %es

// call the trap handler function, giving as first argument the Trapframe we
// constructed earlier. 
push %esp
call trap

// trap() never returns because it ends with an iret in env_pop_tf.

