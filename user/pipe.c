#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	binaryname = "pipe";

	cprintf("got spawned.\n");

	char *data = "hello, world!";
	int fds[2] = {0};

	int r = pipe(fds);

	if (r) {
		panic("pipe failed: %e\n", r);
	}
	cprintf("forking\n");

	r = fork();
	if (r < 0)
		panic("fork failed\n");

	if (r) {
		cprintf("writing to fd..\n");
		write(fds[1], data, strlen(data) + 1); 
		cprintf("OK, wrote.\n");
	}
	else {
		char buf[32];
		int nb = read(fds[0], buf, sizeof(buf));

		if (nb) {
			cprintf("got: '%s'\n", buf);
		} 
		else {
			cprintf("read failed\n");
		}
	}

}

