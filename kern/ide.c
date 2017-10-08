#include <inc/ide.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>

// after finding an IDE disk on the PCI bridge, this function finds the
// device's io_base, which is the port number where we do PIO to talk to the
// disk.
int ide_disk_attach(struct pci_func *pcif) {
	pci_func_enable(pcif);
	io_base = pcif->reg_base[0];
	if (io_base == 0 || io_base == 1)
		io_base = 0x1f0;
	
	return 1;
}
