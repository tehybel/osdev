#include <inc/string.h>
#include <inc/lib.h>

#define PTE_COW		0x800

// prints a map showing the process's address space
// useful for debugging.
void print_process_mappings() {
	int i, j;
	pde_t pde;
	pte_t pte;

	cprintf("--- map for process 0x%x ---\n", sys_getenvid());

	for (i = 0; i < NPDENTRIES; i++) {
		pde = uvpd[i];
		if (!(pde & PTE_P))
			continue;

		for (j = 0; j < NPTENTRIES; j++) {
			void * addr = PGADDR(i, j, 0);
			if (addr >= (void *) UTOP)
				break;

			pte = uvpt[i*NPTENTRIES + j];
			if (!(pte & PTE_P))
				continue;

			cprintf("0x%08x ", addr);
			if (pte & PTE_W)
				cprintf("rw ");
			else if (pte & PTE_COW)
				cprintf("rC ");
			else
				cprintf("r- ");
			cprintf(" -> pa 0x%x\n", PTE_ADDR(pte));
		}
	}

	cprintf("----------------------------\n", sys_getenvid());
}
