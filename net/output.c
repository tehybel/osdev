#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

#define PKTMAP		0x10000000

void
output(envid_t ns_envid)
{
	int r;
	struct jif_pkt *pkt = (struct jif_pkt *)PKTMAP;
	envid_t from_envid = 0;

	binaryname = "ns_output";

	while (1) {
		// read a packet from the network server
		if ((r = ipc_recv(&from_envid, (void *) pkt, NULL)) != NSREQ_OUTPUT) {
			cprintf("warning: output env got an invalid request\n");
			continue;
		}

		if (from_envid != ns_envid) {
			cprintf("warning: output env got IPC from unexpected env %d\n",
					from_envid);
			continue;
		}

		// send the packet to the device driver
		sys_transmit(&pkt->jp_data, pkt->jp_len);
	}
}
