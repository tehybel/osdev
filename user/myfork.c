#include <inc/lib.h>

void b() {
	if (fork() == 0) {
		cprintf("I am B\n");
		exit();
	}
}

void a() {
	if (fork() == 0) {
		cprintf("I am A\n");
		exit();
	}
}


void
umain(int argc, char **argv)
{
	cprintf("I am the parent before A\n");
	a();
	cprintf("I am the parent after A\n");
	b();
	cprintf("I am the parent after B\n");
}

