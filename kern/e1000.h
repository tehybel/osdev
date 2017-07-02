#include <kern/pci.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>

#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

int attach_e1000(struct pci_func *pcif);
int transmit(unsigned char *data, size_t length);

struct txdesc {
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
};


// "The maximum size of an Ethernet packet is 1518 bytes, which bounds how
// big these buffers need to be"
struct txbuf {
	char data[0x600]; // = 1536 decimal
};

#endif	// JOS_KERN_E1000_H
