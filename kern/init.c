/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/monitor.h>
#include <kern/graphics.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/time.h>
#include <kern/pci.h>

#include <kern/e1000.h>

static void boot_aps(void);
static void start_environments();
static void enable_v86();


void i386_init(void) {
	extern char edata[], end[];

	// clear out the .bss
	memset(edata, 0, end - edata);

	// initializes keyboard-, mouse-, and serial-i/o
	init_io();

	init_graphics();

	// initialize the physical page management system 
	// also initialize the page table to support proper virtual memory
	init_memory();

	// user environment initialization
	init_environments();

	// set up the IDT to handle exceptions and other interrupts
	init_idt();

	init_multiprocessing();

	// set up the local and global interrupt controllers
	init_lapic();
	init_pic();

	// init the subsystem responsible for keeping track of time
	init_time();

	// scan the PCI bus and initialize any recognized devices, e.g. the network card
	init_pci_devices();

	// Acquire the big kernel lock before waking up secondary processors
	lock_kernel();
	boot_aps();

	// start all the environments that need to run to provide exokernel services
	start_environments();

	// Should not be necessary - drains keyboard because interrupt has given up.
	drain_keyboard();

	// Schedule and run the first user environment!
	sched_yield();

}


// enable v86 mode extensions. Note that QEMU doesn't support VME.
static void enable_v86() {

	// first ensure that VME is supported on this CPU
	uint32_t edx;

	// EAX = 1: Returns Feature Information in ECX and EDX
	cpuid(1, NULL, NULL, NULL, &edx);

	int vme_supported = edx & 0x2;
	assert (vme_supported);

	lcr4(rcr4() | CR4_VME);
}

static void start_environments() {

	if (have_graphics) {
		ENV_CREATE(user_testgraphics, ENV_TYPE_GRAPHICS);
	}

	// file system process
	ENV_CREATE(fs_fs, ENV_TYPE_FS);

#if !defined(TEST_NO_NS)
	// Start ns.
	ENV_CREATE(net_ns, ENV_TYPE_NS);
#endif

	// used by grading scripts (for testing)
#if defined(TEST)
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	ENV_CREATE(user_icode, ENV_TYPE_USER);
#endif
}

// While boot_aps is booting a given CPU, it communicates the per-core
// stack pointer that should be loaded by mpentry.S to that CPU in
// this variable.
void *mpentry_kstack;

// Start the application processors (APs), i.e., the "secondary processors".
static void boot_aps(void) {
	extern unsigned char mpentry_start[], mpentry_end[];
	void *code;
	int i;
	struct CpuInfo *c;

	// Write entry code to unused memory at MPENTRY_PADDR; we need to do this
	// because APs will boot in real mode, so they need their init code at a
	// low address.
	code = KADDR(MPENTRY_PADDR);

	size_t code_size = mpentry_end - mpentry_start;
	assert(code_size <= PGSIZE); // we only reserved one physical page

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
void mp_main(void) {
	// We are in high EIP now, safe to switch to kern_pgdir 
	lcr3(PADDR(kern_pgdir));
	cprintf("SMP: CPU %d starting\n", cpunum());

	init_lapic();
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
void _panic(const char *file, int line, const char *fmt, ...) {
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
void _warn(const char *file, int line, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	cprintf("kernel warning at %s:%d: ", file, line);
	vcprintf(fmt, ap);
	cprintf("\n");
	va_end(ap);
}
