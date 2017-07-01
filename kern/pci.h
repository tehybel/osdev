#ifndef JOS_KERN_PCI_H
#define JOS_KERN_PCI_H

#include <inc/types.h>

// PCI subsystem interface
enum { pci_res_bus, pci_res_mem, pci_res_io, pci_res_max };

struct pci_bus;

// this struct is initialized when a PCI unit is attached after discovery;
// it is based on Table 4-1 in the Intel e1000 manual.
struct pci_func {
    struct pci_bus *bus;	// Primary bus for bridges

    uint32_t dev; 
    uint32_t func;

    uint32_t dev_id; // device ID
	uint32_t dev_class; // the class code; "020000h identifies the Ethernet
						// controller as an Ethernet adapter."

	// The reg_base and reg_size arrays contain information for up to six Base
	// Address Registers or BARs. reg_base stores the base memory addresses
	// for memory-mapped I/O regions (or base I/O ports for I/O port
	// resources), reg_size contains the size in bytes or number of I/O ports
	// for the corresponding base values from reg_base
    uint32_t reg_base[6]; 
    uint32_t reg_size[6]; 

	// irq_line contains the IRQ line assigned to the device for interrupts
    uint8_t irq_line;
};

struct pci_bus {
    struct pci_func *parent_bridge;
    uint32_t busno;
};

int  pci_init(void);
void pci_func_enable(struct pci_func *f);

#endif
