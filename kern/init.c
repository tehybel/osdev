/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>

static void boot_aps(void);


void
i386_init(void)
{
	extern char edata[], end[];

	// clear out the .bss
	memset(edata, 0, end - edata);

	// initialize the console subsystem; cprintf will not work otherwise
	init_console();

	// initialize the physical page management system 
	// also initialize the page table to support proper virtual memory
	init_memory();

	// user environment initialization
	init_environments();

	// set up the IDT to handle exceptions and other interrupts
	init_idt();

	// Lab 4 multiprocessor initialization functions
	mp_init();
	lapic_init();

	// Lab 4 multitasking initialization functions
	pic_init();

	// Acquire the big kernel lock before waking up APs
	lock_kernel();

	// Starting non-boot CPUs
	boot_aps();

	cprintf("BSP is hanging for now..\n");
	while (1) ; 

#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	ENV_CREATE(user_primes, ENV_TYPE_USER);
#endif // TEST*

	// Schedule and run the first user environment!
	sched_yield();
}

// While boot_aps is booting a given CPU, it communicates the per-core
// stack pointer that should be loaded by mpentry.S to that CPU in
// this variable.
void *mpentry_kstack;

// Start the application processors (APs), i.e., the "secondary processors".
static void
boot_aps(void)
{
	extern unsigned char mpentry_start[], mpentry_end[];
	void *code;
	int i;
	struct CpuInfo *c;

	// Write entry code to unused memory at MPENTRY_PADDR; we need to do this
	// because APs will boot in real mode, so they need their init code at a
	// low address.
	code = KADDR(MPENTRY_PADDR);

	size_t code_size = mpentry_end - mpentry_start;
	assert (code_size <= PGSIZE); // we only reserved one physical page

	memmove(code, mpentry_start, code_size);

	// Boot each AP one at a time
	for (int i = 0; i < ncpu; i++) {
		if (i == cpunum()) {
			// current CPU is already running.
			continue;
		}

		// Tell mpentry.S what stack to use 
		mpentry_kstack = percpu_kstacks[i] + KSTKSIZE;

		// Start the CPU at mpentry_start
		lapic_startap(i, PADDR(code));

		// Wait for the CPU to finish some basic setup in mp_main()
		while (cpus[i].cpu_status != CPU_STARTED)
			; // spin
	}
}

// Setup code for APs
void
mp_main(void)
{
	// We are in high EIP now, safe to switch to kern_pgdir 
	lcr3(PADDR(kern_pgdir));
	cprintf("CMP: CPU %d starting\n", cpunum());

	lapic_init();
	env_init_percpu();
	init_idt_percpu();
	xchg(&thiscpu->cpu_status, CPU_STARTED); // tell boot_aps() we're up

	// Now that we have finished some basic setup, call sched_yield()
	// to start running processes on this CPU.  But make sure that
	// only one CPU can enter the scheduler at a time!
	lock_kernel();
	sched_yield();
}

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void
_panic(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	if (panicstr)
		goto dead;
	panicstr = fmt;

	// Be extra sure that the machine is in as reasonable state
	asm volatile("cli; cld");

	cprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	cprintf("!!!!!\n");
	va_start(ap, fmt);
	cprintf("!!!!! kernel panic on CPU %d at %s:%d: ", cpunum(), file, line);
	vcprintf(fmt, ap);
	va_end(ap);
	cprintf("\n!!!!!\n");
	cprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	cprintf("\nDropping into the monitor.\n");

dead:
	/* break into the kernel monitor */
	while (1)
		monitor(NULL);
}

/* like panic, but don't */
void
_warn(const char *file, int line, const char *fmt,...)
{
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
