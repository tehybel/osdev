#include <inc/lib.h>

void b() {
	if (fork() == 0) {
		cprintf("I am B\n");
		print_process_mappings();
		exit();
	}
}

void a() {
	if (fork() == 0) {
		cprintf("I am A\n");
		print_process_mappings();
		exit();
	}
}


void
umain(int argc, char **argv)
{
	cprintf("I am the parent before A\n");
	print_process_mappings();

	a();
	cprintf("I am the parent after A\n");
	print_process_mappings();

	b();
	cprintf("I am the parent after B\n");
	print_process_mappings();
}

