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
	// we do not currently handle writes that span multiple pages
	assert (PGOFF(utf->utf_fault_va) <= PGSIZE-4);

	void * va = (void *) ROUNDDOWN(utf->utf_fault_va, PGSIZE);
	pte_t pte = uvpt[(uint32_t) va / PGSIZE];

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.

	if (!(utf->utf_err & FEC_WR))
		panic("page fault (not a write) at 0x%x: %e", va, utf->utf_err);
	
	if (!(pte & PTE_COW))
		panic("page fault (write) at 0x%x", va);


	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.

	if (sys_page_alloc(0, PFTEMP, PTE_U | PTE_P | PTE_W))
		panic("sys_page_alloc failed");
	
	memcpy(PFTEMP, va, PGSIZE);

	if (sys_page_map(0, PFTEMP, 0, va, PTE_U | PTE_P | PTE_W))
		panic("sys_page_map failed");
	
	if (sys_page_unmap(0, PFTEMP))
		panic("sys_page_unmap failed");
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
	assert (pte & PTE_U);

	// special case: the exception stack is not dup'd with COW, but is just
	// mapped fresh in the child
	if (va == (void *) UXSTACKBASE) {
		return sys_page_alloc(cid, (void *) UXSTACKBASE,
							  PTE_U | PTE_P | PTE_W);
	}

	if (pte & PTE_SHARE) {
		// if a page table entry has this bit set, the PTE should be copied
		// directly from parent to child. I.e., share the page.
		return sys_page_map(0, va, cid, va, pte & PTE_SYSCALL);
	}
	else if (pte & PTE_COW) {
		assert (!(pte & PTE_W));
		// it's already COW; map it likewise in the child
		return sys_page_map(0, va, cid, va, PTE_U | PTE_P | PTE_COW);
	}
	else if (pte & PTE_W) {
		// it's writable; map the page COW in the child
		if ((result = sys_page_map(0, va, cid, va, PTE_U | PTE_P | PTE_COW)))
			return result;

		// also mark it read-only and COW in the parent (us)
		return sys_page_map(0, va, 0, va, PTE_U | PTE_P | PTE_COW);
	} 
	else {
		// it's read-only; just map the page directly in the child without COW
		return sys_page_map(0, va, cid, va, PTE_U | PTE_P);
	}
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
