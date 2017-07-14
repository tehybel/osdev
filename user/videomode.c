#include <inc/lib.h>

void umain(int argc, char **argv) {
	int r;

	cprintf("hello, graphics\n");

	if ((r = sys_page_alloc(0, (void *) 0, PTE_P|PTE_U|PTE_W)) < 0)
		panic("page_alloc failed: %e\n", r);
	
	cprintf("graphics allocated.\n");

	// int 0x10; int3
	memcpy((void *) 0, "\x90\x90\xcd\x10\xcc", 0x10);

	sys_v86();

	cprintf("graphics survived!\n");
}

