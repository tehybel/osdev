
// this program tests the IPC functionality..

#include <inc/lib.h>

#define PAGE ((void *) 0x40001000)

void sleep(int sec) {
	unsigned now = sys_time_msec();
	unsigned end = now + sec * 1000;

	if ((int)now < 0 && (int)now > -MAXERROR)
		panic("sys_time_msec: %e", (int)now);
	if (end < now)
		panic("sleep: wrap");

	while (sys_time_msec() < end)
		sys_yield();
}

static void child() {
	int32_t val = ipc_recv(NULL, PAGE, NULL);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (val == 0xdeadbeef);

	val = ipc_recv(NULL, PAGE + PGSIZE, NULL);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (strcmp(PAGE + PGSIZE, "GOODBYE, WORLD") == 0);
	assert (val == 0xdeadbee0);

	sleep(2);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (strcmp(PAGE + PGSIZE, "GOODBYE, WORLD") == 0);
	assert (val == 0xdeadbee0);

	cprintf("child is OK\n");
}


static void parent(envid_t cpid) {
	int perm = PTE_P | PTE_U | PTE_W;
	sys_page_alloc(0, PAGE, perm);

	strcpy(PAGE, "HELLO, WORLD");
	ipc_send(cpid, 0xdeadbeef, PAGE, perm);

	sys_page_alloc(0, PAGE + PGSIZE, perm);
	strcpy(PAGE + PGSIZE, "GOODBYE, WORLD");
	ipc_send(cpid, 0xdeadbee0, PAGE + PGSIZE, perm);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (strcmp(PAGE + PGSIZE, "GOODBYE, WORLD") == 0);

	sleep(1);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (strcmp(PAGE + PGSIZE, "GOODBYE, WORLD") == 0);
	
	cprintf("parent is OK\n");

}

void umain(int argc, char **argv) {

	
	envid_t cpid = fork();

	if (cpid == 0) {
		child();
	}
	else {
		parent(cpid);
	}

}

