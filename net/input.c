#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

#define MIN_VA ((void *) 0x10000000)
#define MAX_VA (MIN_VA + 1024*PGSIZE)
void *fresh_va = MIN_VA;

static void *get_fresh_va() {
	void *result = fresh_va;
	fresh_va += PGSIZE;
	if (fresh_va > MAX_VA)
		fresh_va = MIN_VA;
	return result;
}

static void transmit_packet(envid_t ns_envid, void *buf) {
	int r;
	do {
		r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, buf, PTE_U | PTE_P);
		if (r == -E_IPC_NOT_RECV) {
			// the network stack was not ready; retry in a bit.
			sys_yield();
		}
		else if (r) {
			cprintf("warning: input env got an error during IPC: %e\n", r);
		}

	} while (r);
}

void input(envid_t ns_envid) {

	int r;
	binaryname = "ns_input";

	while (1) {
		
		// when we send the packet to the network server via IPC, it needs to
		// read from that page for a while. So we cannot just immediately read
		// a new packet into the same page. That's why we need to allocate a
		// fresh page each time.
		void *buf = get_fresh_va();
		if ((r = sys_page_alloc(0, buf, PTE_U | PTE_P | PTE_W))) {
			cprintf("warning: input environment allocation failed: %e\n", r);
			sys_yield();
			continue;
		}

		struct jif_pkt *pkt = (struct jif_pkt *) buf;

		// read a packet from the device driver
		r = sys_receive(&pkt->jp_data, PGSIZE - sizeof(struct jif_pkt));

		if (r == -E_NOT_READY) {
			// there were no packets; yield and retry later.
			sys_yield();
			continue;
		}
		else if (r == -E_NO_MEM) {
			cprintf("warning: input environment got a too-large packet\n");
			continue;
		}
		else if (r < 0) {
			cprintf("warning: input environment got unhandled error: %e\n", r);
			continue;
		}

		assert (r > 0);
		pkt->jp_len = r;

		// send the packet to the network server
		transmit_packet(ns_envid, buf);

		// unmap the page in this process so it gets cleaned up
		r = sys_page_unmap(0, buf);
		assert (r == 0);
	}
}
