#include <inc/mmu.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/types.h>
#include <inc/env.h>
#include <inc/string.h>

#include <kern/env.h>
#include <kern/pmap.h>

// inserts the pages in [begin, end) from the current env's page table into
// the kernel page table
static void dup_range(void *begin, void *end) {
	void * cur;
	pte_t *pte = NULL;

	end = ROUNDUP(end, PGSIZE);

	assert (begin < (void *) ULIM);
	assert (end < (void *) ULIM);

	for (cur = ROUNDDOWN(begin, PGSIZE); cur < end; cur += PGSIZE) {

		// look it up in the page table
		struct PageInfo* pageinfo = page_lookup(curenv->env_pgdir, cur, &pte);

		// is it not present?
		if (!pageinfo)
			panic("pageinfo not present in dup_range");

		if (page_insert(kern_pgdir, pageinfo, cur, PTE_P | PTE_W))
			panic("bad page_inset in dup_range");
	}
}

void copy_to_user(void *dst, void *src, size_t length) {

	size_t offset;

	user_mem_assert(curenv, dst, length, 0);
	assert (src >= (void *) ULIM);
	assert (src + length >= (void *) ULIM);

	// we are now guaranteed that the source and destination areas don't
	// overlap, since one is in kernelland and the other is in userland.

	// walk over the destination pages and add them to the kernel page table
	// before copying.
	dup_range(dst, dst + length);

	// load new page table, copy the data, load old page table
	uint32_t old_pgdir = rcr3();
	lcr3(PADDR(kern_pgdir));
	memcpy(dst, src, length);
	lcr3(old_pgdir);
}
