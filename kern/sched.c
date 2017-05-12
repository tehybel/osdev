#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// look circularly through 'envs' starting after 'curenv' until we find a
// runnable environment.
struct Env * get_next_runnable_env() {
	int i, offset = curenv ? (curenv - envs) : 0;
	for (i = 0; i < NENV; i++) {
		struct Env * env = &envs[(i + offset) % NENV];
		if (env->env_status == ENV_RUNNABLE)
			return env;
	}
	return NULL;
}

// This function implements simple round-robin scheduling.
// It chooses a user environment and runs it. It never returns.
void
sched_yield(void)
{
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	struct Env * env = get_next_runnable_env();

	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	if (!env && curenv && curenv->env_status == ENV_RUNNING)
		env = curenv;

	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING).
	assert (!env || env->env_status != ENV_RUNNING || env == curenv);

	// if we found an idle environment, switch to it; this never returns.
	if (env)
		env_run(env);

	// otherwise there's nothing to do, so halt the processor.
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

