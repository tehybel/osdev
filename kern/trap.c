#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/env.h>
#include <kern/syscall.h>
#include <kern/sched.h>
#include <kern/kclock.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>

/* For debugging, so print_trapframe can distinguish between printing
 * a saved trapframe and printing the current trapframe and print some
 * additional information in the latter case.
 */
static struct Trapframe *last_tf;

/* Interrupt descriptor table.  (Must be built at run time because
 * shifted function addresses can't be represented in relocation records.)
 */
struct Gatedesc idt[256] = { { 0 } };
struct Pseudodesc idt_pd = {
	sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
	static const char * const excnames[] = {
		"Divide error",
		"Debug",
		"Non-Maskable Interrupt",
		"Breakpoint",
		"Overflow",
		"BOUND Range Exceeded",
		"Invalid Opcode",
		"Device Not Available",
		"Double Fault",
		"Coprocessor Segment Overrun",
		"Invalid TSS",
		"Segment Not Present",
		"Stack Fault",
		"General Protection",
		"Page Fault",
		"(unknown trap)",
		"x87 FPU Floating-Point Error",
		"Alignment Check",
		"Machine-Check",
		"SIMD Floating-Point Exception"
	};

	if (trapno < ARRAY_SIZE(excnames))
		return excnames[trapno];
	if (trapno == T_SYSCALL)
		return "System call";
	if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
		return "Hardware Interrupt";
	return "(unknown trap)";
}

// these are defined in trapentry.S
void trap_divide (); 
void trap_debug  (); 
void trap_nmi    (); 
void trap_brkpt  (); 
void trap_oflow  (); 
void trap_bound  (); 
void trap_illop  (); 
void trap_device (); 
void trap_dblflt (); 
void trap_tss    (); 
void trap_segnp  (); 
void trap_stack  (); 
void trap_gpflt  (); 
void trap_pgflt  (); 
void trap_fperr  (); 
void trap_align  (); 
void trap_mchk   (); 
void trap_simerr (); 

void trap_syscall (); 

void badint (); 

// this function initializes the IDT so that we can handle exceptions from the
// processor.
void
init_idt(void)
{
	extern struct Segdesc gdt[];

	// set default handling for all interrupts
	int i;
	for (i = 0; i < ARRAY_SIZE(idt); i++) {
		SETGATE (idt[i], 0, GD_KT, badint, 0);
	}

	// now set some specific handlers;
	// these are defined in kern/trapentry.S

	//                    istrap
	//       interruptnum      sel    handler      privlvl
	SETGATE (idt[T_DIVIDE],  0, GD_KT, trap_divide,  0) // divide error
	SETGATE (idt[T_DEBUG],   1, GD_KT, trap_debug,   0) // debug exception
	SETGATE (idt[T_NMI],     0, GD_KT, trap_nmi,     0) // non-maskable interrupt
	SETGATE (idt[T_BRKPT],   1, GD_KT, trap_brkpt,   3) // breakpoint
	SETGATE (idt[T_OFLOW],   1, GD_KT, trap_oflow,   0) // overflow
	SETGATE (idt[T_BOUND],   0, GD_KT, trap_bound,   0) // bounds check
	SETGATE (idt[T_ILLOP],   0, GD_KT, trap_illop,   0) // illegal opcode
	SETGATE (idt[T_DEVICE],  0, GD_KT, trap_device,  0) // device not available
	SETGATE (idt[T_DBLFLT],  0, GD_KT, trap_dblflt,  0) // double fault
	SETGATE (idt[T_TSS],     0, GD_KT, trap_tss,     0) // invalid task switch seg
	SETGATE (idt[T_SEGNP],   0, GD_KT, trap_segnp,   0) // segment not present
	SETGATE (idt[T_STACK],   0, GD_KT, trap_stack,   0) // stack exception
	SETGATE (idt[T_GPFLT],   0, GD_KT, trap_gpflt,   0) // general protection fault
	SETGATE (idt[T_PGFLT],   0, GD_KT, trap_pgflt,   0) // page fault
	SETGATE (idt[T_FPERR],   0, GD_KT, trap_fperr,   0) // floating point error
	SETGATE (idt[T_ALIGN],   0, GD_KT, trap_align,   0) // aligment check
	SETGATE (idt[T_MCHK],    0, GD_KT, trap_mchk,    0) // machine check
	SETGATE (idt[T_SIMDERR], 0, GD_KT, trap_simerr,  0)	// SIMD floating point err

	SETGATE (idt[T_SYSCALL], 0, GD_KT, trap_syscall, 3) // syscalls

	init_idt_percpu();
}

// Initialize and load the per-CPU TSS and IDT
void
init_idt_percpu(void)
{
	// The example code here sets up the Task State Segment (TSS) and
	// the TSS descriptor for CPU 0. But it is incorrect if we are
	// running on other CPUs because each CPU has its own kernel stack.
	// Fix the code so that it works for all CPUs.
	//
	// Hints:
	//   - The macro "thiscpu" always refers to the current CPU's
	//     struct CpuInfo;
	//   - The ID of the current CPU is given by cpunum() or
	//     thiscpu->cpu_id;
	//   - Use "thiscpu->cpu_ts" as the TSS for the current CPU,
	//     rather than the global "ts" variable;
	//   - Use gdt[(GD_TSS0 >> 3) + i] for CPU i's TSS descriptor;
	//   - You mapped the per-CPU kernel stacks in mem_init_mp()
	//
	// ltr sets a 'busy' flag in the TSS selector, so if you
	// accidentally load the same TSS on more than one CPU, you'll
	// get a triple fault.  If you set up an individual CPU's TSS
	// wrong, you may not get a fault until you try to return from
	// user space on that CPU.
	//
	// LAB 4: Your code here:

	struct Taskstate *ts = &thiscpu->cpu_ts;
	uintptr_t stack_top = KSTACKTOP - cpunum()*PERSTACK_SIZE;
	int GD_TSSi = GD_TSS0 + (cpunum() << 3);

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	ts->ts_esp0 = stack_top;
	ts->ts_ss0 = GD_KD;
	ts->ts_iomb = sizeof(struct Taskstate);

	// Initialize the TSS slot of the gdt.
	gdt[GD_TSSi >> 3] = SEG16(STS_T32A, (uint32_t) (ts),
								sizeof(struct Taskstate) - 1, 0);
	gdt[GD_TSSi >> 3].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSSi);

	// Load the IDT
	lidt(&idt_pd);
}

void
print_trapframe(struct Trapframe *tf)
{
	cprintf("TRAP frame at %p from CPU %d\n", tf, cpunum());
	print_regs(&tf->tf_regs);
	cprintf("  es   0x----%04x\n", tf->tf_es);
	cprintf("  ds   0x----%04x\n", tf->tf_ds);
	cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
	// If this trap was a page fault that just happened
	// (so %cr2 is meaningful), print the faulting linear address.
	if (tf == last_tf && tf->tf_trapno == T_PGFLT)
		cprintf("  cr2  0x%08x\n", rcr2());
	cprintf("  err  0x%08x", tf->tf_err);
	// For page faults, print decoded fault error code:
	// U/K=fault occurred in user/kernel mode
	// W/R=a write/read caused the fault
	// PR=a protection violation caused the fault (NP=page not present).
	if (tf->tf_trapno == T_PGFLT)
		cprintf(" [%s, %s, %s]\n",
			tf->tf_err & 4 ? "user" : "kernel",
			tf->tf_err & 2 ? "write" : "read",
			tf->tf_err & 1 ? "protection" : "not-present");
	else
		cprintf("\n");
	cprintf("  eip  0x%08x\n", tf->tf_eip);
	cprintf("  cs   0x----%04x\n", tf->tf_cs);
	cprintf("  flag 0x%08x\n", tf->tf_eflags);
	if ((tf->tf_cs & 3) != 0) {
		cprintf("  esp  0x%08x\n", tf->tf_esp);
		cprintf("  ss   0x----%04x\n", tf->tf_ss);
	}
}

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

// this function handles processor exceptions based on their type.
static void
trap_dispatch(struct Trapframe *tf)
{
	// page faults are handled specially
	if (tf->tf_trapno == T_PGFLT) {
		page_fault_handler(tf);
		return;
	}

	// Handle spurious interrupts
	// The hardware sometimes raises these because of noise on the
	// IRQ line or other reasons. We don't care.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SPURIOUS) {
		cprintf("Spurious interrupt on irq 7\n");
		print_trapframe(tf);
		return;
	}

	// breakpoints invoke the kernel monitor
	if (tf->tf_trapno == T_BRKPT) {
		monitor(tf); // never returns
	}

	// handle syscalls
	if (tf->tf_trapno == T_SYSCALL) {
		int32_t retval = syscall(
			tf->tf_regs.reg_eax, 
			tf->tf_regs.reg_edx, 
			tf->tf_regs.reg_ecx, 
			tf->tf_regs.reg_ebx, 
			tf->tf_regs.reg_edi, 
			tf->tf_regs.reg_esi
		);
		tf->tf_regs.reg_eax = retval;
		return;
	}

	// Handle clock interrupts. Don't forget to acknowledge the
	// interrupt using lapic_eoi() before calling the scheduler!
	// LAB 4: Your code here.

	assert (tf->tf_cs != GD_KT); // should have been caught earlier.

	// unhandled trap; display it, then terminate the environment.
	print_trapframe(tf);
	env_destroy(curenv);
}

// this function is called when an exception occurs.
void
trap(struct Trapframe *tf)
{
	// The environment may have set DF and some versions
	// of GCC rely on DF being clear
	asm volatile("cld" ::: "cc");

	// Halt the CPU if some other CPU has called panic()
	extern char *panicstr;
	if (panicstr)
		asm volatile("hlt");

	// Re-acqurie the big kernel lock if we were halted in
	// sched_yield()
	if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
		lock_kernel();

	// Check that interrupts are disabled.  If this assertion
	// fails, DO NOT be tempted to fix it by inserting a "cli" in
	// the interrupt path.
	assert(!(read_eflags() & FL_IF));

	// if we trapped from kernel land, panic.
	if ((tf->tf_cs & 3) != 3 || tf->tf_cs == GD_KT) {
		print_trapframe(tf);
		panic("trap in kernel mode");
	}

	// if we get here, we trapped from user mode.
	assert ((tf->tf_cs & 3) == 3);

	// Acquire the big kernel lock before doing any
	// serious kernel work.
	lock_kernel();

	assert (curenv);
	assert (curenv->env_status == ENV_RUNNING);

	// Garbage collect if current environment is a zombie
	if (curenv->env_status == ENV_DYING) {
		env_free(curenv);
		curenv = NULL;
		sched_yield();
	}

	// Copy trap frame (which is currently on the stack)
	// into 'curenv->env_tf', so that running the environment
	// will restart at the trap point.
	curenv->env_tf = *tf;

	// The trapframe on the stack should be ignored from here on.
	tf = &curenv->env_tf;

	// Record that tf is the last real trapframe so
	// print_trapframe can print some additional information.
	last_tf = tf;

	// Dispatch based on what type of trap occurred
	trap_dispatch(tf);

	// If we made it to this point, then no other environment was
	// scheduled, so we should return to the current environment
	// if doing so makes sense.
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
}

// 
// Call the environment's page fault upcall, if one exists.  Set up a
// page fault stack frame on the user exception stack (below
// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
//
// The page fault upcall might cause another page fault, in which case
// we branch to the page fault upcall recursively, pushing another
// page fault stack frame on top of the user exception stack.
//
// If there's no page fault upcall, the environment didn't allocate a
// page for its exception stack or can't write to it, or the exception
// stack overflows, then destroy the environment that caused the fault.
// 
void
page_fault_handler(struct Trapframe *tf)
{
	// kernel-mode page faults will already have been caught, so any page
	// faults at this point are in user mode.

	// Read processor's CR2 register to find the faulting address
	uint32_t fault_va = rcr2();

	// no handler? Then destroy the env.
	if (curenv->env_pgfault_upcall == NULL)
		goto destroy_env;

	// figure out where to put the new stack frame
	uintptr_t new_esp;
	if (tf->tf_esp >= UXSTACKBASE && tf->tf_esp < UXSTACKTOP) {
		// the page fault handler has faulted recursively; we will put the
		// trap-time state below the old stack frame
		new_esp = tf->tf_esp;
	} else {
		// normal fault; we will put the trap-time state below the top of the
		// exception stack
		new_esp = UXSTACKTOP;
	}

	// make sure there's space for the new trap-time state; if not, it's
	// because the exception stack overflowed or it was mapped non-writable.
	// Then the environment will be destroyed and user_mem_assert doesn't
	// return.
	size_t needed_size = sizeof(struct UTrapframe) + sizeof(uint32_t);
	user_mem_assert(curenv, (void *) new_esp - needed_size, 
		needed_size, PTE_W);

	// map the exception stack into our page table so we can modify it from
	// kernel land
	struct PageInfo *pinfo;
	pinfo = page_lookup(curenv->env_pgdir, (void *) UXSTACKBASE, NULL);
	assert (pinfo); // should exist since we just did user_mem_assert
	// the UXSTACKBASE page should never be shared
	assert (pinfo->pp_ref == 1); 

	if (page_insert(kern_pgdir, pinfo, (void *) UXSTACKBASE, PTE_W))
		goto destroy_env;
	assert (pinfo->pp_ref == 2);

	// push empty word
	new_esp -= sizeof(uint32_t);
	*(uint32_t *) new_esp = 0;

	// push UTrapFrame
	new_esp -= sizeof(struct UTrapframe);
	struct UTrapframe *utf = (struct UTrapframe *) new_esp;
	utf->utf_fault_va = fault_va;
	utf->utf_err = tf->tf_err;
	utf->utf_regs = tf->tf_regs;
	utf->utf_eip = tf->tf_eip;
	utf->utf_eflags = tf->tf_eflags;
	utf->utf_esp = tf->tf_esp;

	// no need to keep the page around in the kernel page table
	page_remove(kern_pgdir, (void *) UXSTACKBASE);
	assert (pinfo->pp_ref == 1); 

	// finally return to user-mode, but branch to the page fault handler
	assert (tf == &curenv->env_tf);
	tf->tf_eip = (uintptr_t) curenv->env_pgfault_upcall;
	tf->tf_esp = new_esp;
	env_run(curenv); // never returns

destroy_env:
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv); // never returns
}

