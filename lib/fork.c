// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void _pgfault_upcall(void);

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	panic("pgfault not implemented");
}

//
// Map our virtual page page_number into the child environment given by 'cid'
// at the same virtual address. If the page is writable or copy-on-write, the
// new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well. 
//
// Returns: 0 on success, < 0 on error
//
static int
duppage(envid_t cid, unsigned int page_number)
{
	int result;
	void * va = (void *) (page_number * PGSIZE);
	pte_t pte = uvpt[page_number];
	assert (pte & PTE_P);

	// special case: the exception stack is not dup'd with COW, but is just
	// mapped fresh in the child
	if (va == (void *) UXSTACKBASE) {
		return sys_page_alloc(cid, (void *) UXSTACKBASE, 
							  PTE_U | PTE_P | PTE_W);
	}

	if ((pte & PTE_W) || (pte & PTE_COW)) {
		// it's writable; map the page COW in the child
		if ((result = sys_page_map(0, va, cid, va, PTE_U | PTE_P | PTE_COW)))
			return result;

		// also mark it read-only and COW in the parent (us)
		if ((result = sys_page_map(0, va, 0, va, PTE_U | PTE_P | PTE_COW)))
			return result;
	} 
	else {
		// it's read-only; just map the page directly in the child without COW
		if ((result = sys_page_map(0, va, cid, va, PTE_U | PTE_P)))
			return result;
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
envid_t
fork(void)
{
	envid_t cid;
	pte_t pte;
	pde_t pde;
	unsigned int page_number;
	int i, j;

	set_pgfault_handler(pgfault);
	
	cid = sys_exofork();
	if (cid < 0)
		panic("fork failed: %e", cid);
	
	if (cid == 0) {
		// this is the child; update 'thisenv' and return
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// now walk over the page directory, finding entries which are present,
	// and dup those to the child
	for (i = 0; i < NPDENTRIES; i++) {
		pde = uvpd[i];
		if (!(pde & PTE_P))
			continue;

		for (j = 0; j < NPTENTRIES; j++) {
			if (PGADDR(i, j, 0) >= (void *) UTOP) {
				// we only need to check up to UTOP
				break;
			}

			page_number = i*NPTENTRIES + j;
			pte = uvpt[page_number];
			if (!(pte & PTE_P))
				continue;

			if (duppage(cid, page_number))
				panic("duppage failed");
		}
	}

	// set the page fault handler for the child, too
	if (sys_env_set_pgfault_upcall(cid, _pgfault_upcall))
		panic("sys_env_set_pgfault_upcall failed");

	// mark the child as runnable
	if (sys_env_set_status(cid, ENV_RUNNABLE))
		panic("sys_env_set_status failed");

	return cid;
}


// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
