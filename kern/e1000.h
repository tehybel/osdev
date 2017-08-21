#include <kern/pci.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>

#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

int e1000_attach(struct pci_func *pcif);
int e1000_transmit(unsigned char *data, size_t length);
int e1000_receive(unsigned char *buf, size_t bufsize);

struct txdesc {
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
};

struct rxdesc {
	uint64_t addr;
	uint16_t length;
	uint16_t checksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
};

int e1000_initialized;


// "The maximum size of an Ethernet packet is 1518 bytes, which bounds how
// big these buffers need to be"
struct txbuf {
	char data[0x600]; // = 1536 decimal
};

struct rxbuf {
	char data[2048]; // NOTE: if you change this, change BSIZE in receive_init
};

#endif	// JOS_KERN_E1000_H
