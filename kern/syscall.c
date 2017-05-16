/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	struct Env *env = NULL;
	envid_t pid = curenv->env_id;
	int result;

	if ((result = env_alloc(&env, pid)))
		return result;
	
	env->env_status = ENV_NOT_RUNNABLE;

	// the env should have the same register state as the parent
	env->env_tf = curenv->env_tf;

	// we should make sys_exofork return 0 in the child
	env->env_tf.tf_regs.reg_eax = 0;

	return env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	struct Env *env = NULL;
	int result = 0;

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;

	if ((result = envid2env(envid, &env, 1)))
		return result;
	
	env->env_status = status;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	struct Env *env = NULL;
	int result = 0;

	if ((result = envid2env(envid, &env, 1)))
		return result;
	
	// TODO should we check 'func' here or elsewhere?
	env->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	struct Env *env = NULL;
	struct PageInfo *pinfo = NULL;
	int result = 0;

	// PTE_U and PTE_P must be set
	if ((perm & (PTE_U | PTE_P)) != (PTE_U | PTE_P))
		return -E_INVAL;
	
	// PTE_AVAIL and PTE_W may be set, nothing else may be set
	if ((perm & PTE_SYSCALL) != perm)
		return -E_INVAL;
	
	// is the 'va' OK?
	if (va >= (void *) UTOP || PGOFF(va) != 0)
		return -E_INVAL;

	if ((result = envid2env(envid, &env, 1)))
		return result;
	
	if (!(pinfo = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;
	
	// page_insert increases the page refcnt on success
	if ((result = page_insert(env->env_pgdir, pinfo, va, perm))) {
		page_free(pinfo);
		return result;
	}

	assert (page_lookup(env->env_pgdir, va, NULL));
	assert (pinfo->pp_ref == 1);
	
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL if srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t src_envid, void *src_va,
	     envid_t dst_envid, void *dst_va, int dst_perm)
{
	struct Env *src_env = NULL, *dst_env = NULL;
	struct PageInfo *pinfo = NULL;
	pte_t *pte = NULL;
	int result = 0;

	// check that the environments exist and we have permissions for them
	if ((result = envid2env(src_envid, &src_env, 1)))
		return result;
	if ((result = envid2env(dst_envid, &dst_env, 1)))
		return result;

	// check that the addresses are OK
	if (src_va >= (void *) UTOP || PGOFF(src_va) != 0)
		return -E_INVAL;
	if (dst_va >= (void *) UTOP || PGOFF(dst_va) != 0)
		return -E_INVAL;

	// check the permissions
	if ((dst_perm & (PTE_U | PTE_P)) != (PTE_U | PTE_P))
		return -E_INVAL;
	if ((dst_perm & PTE_SYSCALL) != dst_perm)
		return -E_INVAL;
	
	if (!(pinfo = page_lookup(src_env->env_pgdir, src_va, &pte)))
		return -E_INVAL;
	
	assert (pinfo->pp_ref > 0);
	
	// we do not allow setting a read-only page to be writable
	if ((dst_perm & PTE_W) && !(*pte & PTE_W))
		return -E_INVAL;
	
	return page_insert(dst_env->env_pgdir, pinfo, dst_va, dst_perm);
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	struct Env *env = NULL;
	int result = 0;
	
	if (va >= (void *) UTOP || PGOFF(va) != 0)
		return -E_INVAL;

	if ((result = envid2env(envid, &env, 1)))
		return result;
	
	page_remove(env->env_pgdir, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	panic("sys_ipc_try_send not implemented");
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	panic("sys_ipc_recv not implemented");
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	
	// cprintf("syscall %d (0x%x, 0x%x, 0x%x, 0x%x, 0x%x)\n", 
	// 	syscallno, a1, a2, a3, a4, a5);

	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((const char *) a1, a2);
		return 0;
	
	case SYS_cgetc:
		return sys_cgetc();
	
	case SYS_getenvid:
		return sys_getenvid();
	
	case SYS_env_destroy:
		return sys_env_destroy(a1);
	
	case SYS_yield:
		sys_yield(); // never returns
		return 0;
	
	case SYS_exofork:
		return sys_exofork();
	
	case SYS_env_set_status:
		return sys_env_set_status(a1, a2);
	
	case SYS_page_alloc:
		return sys_page_alloc(a1, (void *) a2, a3);
	
	case SYS_page_map:
		return sys_page_map(a1, (void *) a2, a3, (void *) a4, a5);
	
	case SYS_page_unmap:
		return sys_page_unmap(a1, (void *) a2);
	
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall(a1, (void *) a2);

	default:
		return -E_INVAL;

	}
}

