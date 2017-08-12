
// this program tests the IPC functionality..

#include <inc/lib.h>

#define PAGE ((void *) 0x40001000)

static void child() {
	int32_t val = ipc_recv(NULL, PAGE, NULL);

	assert (strcmp(PAGE, "HELLO, WORLD") == 0);
	assert (val == 0xdeadbeef);

}


static void parent(envid_t cpid) {
	int perm = PTE_P | PTE_U | PTE_W;
	sys_page_alloc(0, PAGE, perm);

	strcpy(PAGE, "HELLO, WORLD");
	ipc_send(cpid, 0xdeadbeef, PAGE, perm);
	

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

