#include <kern/e1000.h>

/*
	Driver for the e1000 network adapter which QEMU emulates.

	All references to tables and sections in this driver code refer to the
	Intel e1000 manual ("PCI/PCI-X Family of Gigabit Ethernet Controllers
	Software Developer’s Manual")

*/

physaddr_t e1000_pa;        // Initialized in mpconfig.c
volatile uint32_t *e1000_va;

#define TXDESC_ARRAY_SIZE 64

struct txdesc 
txdesc_array [TXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));

struct txbuf 
txbuffers [TXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));


#define TDBAL_OFFSET (0x3800/sizeof(uint32_t))
#define TDBAL e1000_va[TDBAL_OFFSET]

#define TDBAH_OFFSET (0x3804/sizeof(uint32_t))
#define TDBAH e1000_va[TDBAH_OFFSET]

#define TDLEN_OFFSET (0x3808/sizeof(uint32_t))
#define TDLEN e1000_va[TDLEN_OFFSET]

#define TDH_OFFSET (0x3810/sizeof(uint32_t))
#define TDH e1000_va[TDH_OFFSET]

#define TDT_OFFSET (0x3818/sizeof(uint32_t))
#define TDT e1000_va[TDT_OFFSET]

// Transmit Control Register
#define TCTL_OFFSET (0x400/sizeof(uint32_t))
#define TCTL e1000_va[TCTL_OFFSET]
#define TCTL_EN_BITOFF 1
#define TCTL_PSP_BITOFF 3
#define TCTL_CT_BITOFF 4
#define TCTL_COLD_BITOFF 12

// Transmit IPG Register
// This register controls the IPG (Inter Packet Gap) timer for the Ethernet
// controller.
#define TIPG_OFFSET (0x410/sizeof(uint32_t))
#define TIPG e1000_va[TIPG_OFFSET]
#define TIPG_IPGT_BITOFF 0
#define TIPG_IPGR1_BITOFF 10
#define TIPG_IPGR2_BITOFF 20

// prepares the e1000 for transmission. The process is described in Section
// 14.5 and is copied into the comments here.
void transmit_init() {
	int i;

	// 14.5: Transmit Initialization

	// Allocate a region of memory for the transmit descriptor list. Software
	// should insure this memory is aligned on a paragraph (16-byte) boundary.
	// (Since TDLEN must be 128-byte aligned and each transmit descriptor is
	// 16 bytes, the transmit descriptor array will need some multiple of 8
	// transmit descriptors.)
	assert (sizeof(struct txdesc) == 16);
	assert ((TXDESC_ARRAY_SIZE % 8) == 0);
	assert (((TXDESC_ARRAY_SIZE * sizeof(struct txdesc)) % 128) == 0);

	// we've already allocated the txdesc array as a global variable, so just
	// take its physical address.
	physaddr_t txdesc_array_pa = PADDR(txdesc_array);

	// however we have not initialized the txdesc structs, so we should do
	// that now.
	for (i = 0; i < TXDESC_ARRAY_SIZE; i++) {
		txdesc_array[i].addr = PADDR(&txbuffers[i]);
		txdesc_array[i].length = sizeof(struct txbuf);
	}

	
	// Program the Transmit Descriptor Base Address (TDBAL/TDBAH) register(s)
	// with the address of the region. TDBAL is used for 32-bit addresses and
	// both TDBAL and TDBAH are used for 64-bit addresses.
	TDBAL = txdesc_array_pa;
	TDBAH = 0;

	// Set the Transmit Descriptor Length (TDLEN) register to the size (in
	// bytes) of the descriptor ring. This register must be 128-byte aligned.
	TDLEN = sizeof(struct txdesc) * TXDESC_ARRAY_SIZE;

	// The Transmit Descriptor Head and Tail (TDH/TDT) registers are
	// initialized (by hardware) to 0b after a power-on or a software
	// initiated Ethernet controller reset. Software should write 0b to both
	// these registers to ensure this.
	TDH = TDT = 0;

	// Initialize the Transmit Control Register (TCTL) for desired operation to
	// include the following:
	// - Set the Enable (TCTL.EN) bit to 1b for normal operation.
	// - Set the Pad Short Packets (TCTL.PSP) bit to 1b.
	// - Configure the Collision Threshold (TCTL.CT) to the desired value.
	//   Ethernet standard is 10h. This setting only has meaning in half duplex
	//   mode.
	// - Configure the Collision Distance (TCTL.COLD) to its expected value.
	//   For full duplex operation, this value should be set to 40h. For gigabit
	//   half duplex, this value should be set to 200h. For 10/100 half duplex,
	//   this value should be set to 40h.
	TCTL = (1 << TCTL_EN_BITOFF) 
		 | (1 << TCTL_PSP_BITOFF) 
		 | (0x10 << TCTL_CT_BITOFF) 
		 | (0x40 << TCTL_COLD_BITOFF);

	// Program the Transmit IPG (TIPG) register with the following decimal
	// values to get the minimum legal Inter Packet Gap: <not included>.
	// Actually we use those from Table 13-77 instead:
	// "For the IEEE 802.3 standard IPG value of 96-bit time, the value
	// that should be programmed into IPGT is 10"
	// "For the IEEE 802.3 standard IPG value of 96-bit time, the value
	// that should be programmed into IPGR2 is six"
	// "According to the IEEE802.3 standard, IPGR1 should be 2/3 of
	// IPGR2 value."
	TIPG = (10 << TIPG_IPGT_BITOFF)
		 | (4 << TIPG_IPGR1_BITOFF)
		 | (6 << TIPG_IPGR2_BITOFF);

}

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

	transmit_init();

	return 0;
}

