#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int fd, n, r;
	char buf[512+1];

	binaryname = "icode";

	if ((fd = open("/motd", O_RDONLY)) < 0)
		panic("icode: open /motd: %e", fd);

	while ((n = read(fd, buf, sizeof buf-1)) > 0)
		sys_cputs(buf, n);

	close(fd);

	if ((r = spawnl("/init", "init", "initarg1", "initarg2", (char*)0)) < 0)
		panic("icode: spawn /init: %e", r);
}
