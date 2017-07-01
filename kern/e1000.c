#include <kern/e1000.h>

/*
	Driver for the e1000 network adapter which QEMU emulates

*/

physaddr_t e1000_pa;        // Initialized in mpconfig.c
volatile uint32_t *e1000_va;

int attach_e1000(struct pci_func *pcif) {
	// enable the device; this negotiates a PA at which we can do MMIO to talk
	// to the device. The PA and size go in BAR0.
	pci_func_enable(pcif);
	e1000_pa = pcif->reg_base[0];
	size_t size = pcif->reg_size[0];

	// now map a VA to the PA, so we can do MMIO
	e1000_va = mmio_map_region(e1000_pa, size);

	// The device status register is at byte offset 8; it should contain
	// 0x80080783, which means "full duplex link up, 1000 MB/s" and more.
	assert (e1000_va[2] == 0x80080783);

	return 0;
}

