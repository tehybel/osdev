#ifndef JOS_INC_IDE_H
#define JOS_INC_IDE_H

#include <kern/pci.h>
#include <kern/pcireg.h>

int ide_disk_attach(struct pci_func *pcif);

uint32_t io_base;

#endif	// not JOS_INC_FD_H
