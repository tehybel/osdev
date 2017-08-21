#include <kern/e1000.h>
#include <inc/string.h>
#include <inc/error.h>
#include <kern/copy.h>

/*
	Driver for the e1000 network adapter which QEMU emulates.

	All references to tables and sections in this driver code refer to the
	Intel e1000 manual ("PCI/PCI-X Family of Gigabit Ethernet Controllers
	Software Developer’s Manual")

*/

physaddr_t e1000_pa;        // Initialized in mpconfig.c
volatile uint32_t *e1000_va;

// number of transmit descriptors
#define TXDESC_ARRAY_SIZE 64

// number of receive descriptors
#define RXDESC_ARRAY_SIZE 128

struct txdesc
txdesc_array [TXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));

struct txbuf 
txbuffers [TXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));

struct rxdesc
rxdesc_array [RXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));

struct rxbuf 
rxbuffers [RXDESC_ARRAY_SIZE]
__attribute__ ((aligned (16)));

// ----- various offsets into structures are defined below -----

#define TXDESC_STATUS_DD_BITOFF 0
// "RS tells the hardware to report the status information for this
// descriptor."
#define TXDESC_CMD_EOP_BITOFF 0
#define TXDESC_CMD_RS_BITOFF 3

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


#define RAL e1000_va[0x5400/sizeof(uint32_t)]
#define RAH e1000_va[0x5404/sizeof(uint32_t)]
#define RAH_AV_BITOFF 31
#define IMS e1000_va[0xd0/sizeof(uint32_t)]
#define MTA e1000_va[0x5200/sizeof(uint32_t)]
#define RDBAL e1000_va[0x2800/sizeof(uint32_t)]
#define RDBAH e1000_va[0x2804/sizeof(uint32_t)]
#define RDLEN e1000_va[0x2808/sizeof(uint32_t)]
#define RDH e1000_va[0x2810/sizeof(uint32_t)]
#define RDT e1000_va[0x2818/sizeof(uint32_t)]
#define RCTL e1000_va[0x100/sizeof(uint32_t)]
#define RCTL_EN_BITOFF 1
#define RCTL_SBP_BITOFF 2
#define RCTL_UPE_BITOFF 3
#define RCTL_MPE_BITOFF 4
#define RCTL_LPE_BITOFF 5
#define RCTL_LBM_BITOFF 6
#define RCTL_BAM_BITOFF 15
#define RCTL_SECRC_BITOFF 26
#define RXDESC_STATUS_DD_BITOFF 0
#define RXDESC_STATUS_EOP_BITOFF 1


// ---------------------------------------------------

#define BIT(bitoff) (1<<(bitoff))
#define CLEAR_BIT(var, bitoff) ((var) &= ~(BIT(bitoff)))
#define SET_BIT(var, bitoff) ((var) |= (BIT(bitoff)))
#define BIT_IS_SET(var, bitoff) ((var) & (BIT(bitoff)))

// Since we set the RS bit on the descriptors, the network card will set
// the DD bit once a descriptor has been consumed. 
static int descriptor_is_in_use(struct txdesc *desc) {
	return !BIT_IS_SET(desc->status, TXDESC_STATUS_DD_BITOFF);
}

static void mark_descriptor_in_use(struct txdesc *desc) {
	CLEAR_BIT(desc->status, TXDESC_STATUS_DD_BITOFF);
}


// prepares the e1000 for transmission. The process is described in Section
// 14.5 and is copied into the comments here.
static void e1000_tx_init() {
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

	memset(txdesc_array, '\0', sizeof(struct txdesc) * TXDESC_ARRAY_SIZE);

	// however we have not initialized the txdesc structs, so we should do
	// that now.
	for (i = 0; i < TXDESC_ARRAY_SIZE; i++) {
		struct txdesc *desc = &txdesc_array[i];
		desc->addr = PADDR(&txbuffers[i]);

		// set the RS bit so that the e1000 will let us know once it's
		// consumed a descriptor
		SET_BIT(desc->cmd, TXDESC_CMD_RS_BITOFF);

		// set the DD bit to indicate that this descriptor is safe to use
		SET_BIT(desc->status, TXDESC_STATUS_DD_BITOFF);
		assert (!descriptor_is_in_use(desc));

		// set the EOP bit once and for all, because all packets will fit in
		// one descriptor
		SET_BIT(desc->cmd, TXDESC_CMD_EOP_BITOFF);
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
	SET_BIT(TCTL, TCTL_EN_BITOFF);
	SET_BIT(TCTL, TCTL_PSP_BITOFF);
	TCTL |= (0x10 << TCTL_CT_BITOFF);
	TCTL |= (0x40 << TCTL_COLD_BITOFF);

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


// prepare the e1000 to receive packets; we need to set certain
// registers/flags and set up the receive queue. Most comments are from
// Section 14.4.
static void e1000_rx_init() {
	int i;

	// Program the Receive Address Register(s) (RAL/RAH) with the desired
	// Ethernet addresses. RAL[0]/RAH[0] should always be used to store the
	// Individual Ethernet MAC address of the Ethernet controller.

	// For now, hardcode this to 52:54:00:12:34:56 which is the QEMU default.
	RAL = 0x12005452;
	RAH = 0x00005634 | BIT(RAH_AV_BITOFF);
	// RAL = 0x52540012;
	// RAH = 0x34560000;

	// Initialize the MTA (Multicast Table Array) to 0b. Per software, entries
	// can be added to this table as desired.
	// "The multicast table array is a way to extend address filtering"
	volatile uint32_t *ptr = &MTA;
	for (i = 0; i < 128; i++)
		ptr[i] = 0;

	// Program the Interrupt Mask Set/Read (IMS) register to enable any
	// interrupt the software driver wants to be notified of when the event
	// occurs. Suggested bits include RXT, RXO, RXDMT, RXSEQ, and LSC. There
	// is no immediate reason to enable the transmit interrupts.
	IMS = 0; // no interrupts for now

	// If software uses the Receive Descriptor Minimum Threshold Interrupt,
	// the Receive Delay Timer (RDTR) register should be initialized with the
	// desired delay time.
	// do nothing for now

	// Allocate a region of memory for the receive descriptor list. Software
	// should insure this memory is aligned on a paragraph (16-byte) boundary.
	// Program the Receive Descriptor Base Address (RDBAL/RDBAH) register(s)
	// with the address of the region. 
	assert (sizeof(struct rxdesc) == 16);
	memset(rxdesc_array, '\0', RXDESC_ARRAY_SIZE * sizeof(struct rxdesc));
	RDBAL = PADDR(rxdesc_array);
	RDBAH = 0;

	// Set the Receive Descriptor Length (RDLEN) register to the size (in
	// bytes) of the descriptor ring. This register must be 128-byte aligned.
	assert ((RXDESC_ARRAY_SIZE * sizeof(struct rxdesc)) % 128 == 0);
	RDLEN = RXDESC_ARRAY_SIZE * sizeof(struct rxdesc);

	// Receive buffers of appropriate size should be allocated and pointers to
	// these buffers should be stored in the receive descriptor ring. 
	for (i = 0; i < RXDESC_ARRAY_SIZE; i++) {
		struct rxdesc *desc = &rxdesc_array[i];
		desc->addr = PADDR(&rxbuffers[i]);
	}
	
	// Software initializes the Receive Descriptor Head (RDH) register and
	// Receive Descriptor Tail (RDT) with the appropriate head and tail
	// addresses. Head should point to the first valid receive descriptor in
	// the descriptor ring and tail should point to one descriptor beyond the
	// last valid descriptor in the descriptor ring.
	RDT = RXDESC_ARRAY_SIZE - 1;
	RDH = 0;

	// Program the Receive Control (RCTL) register with appropriate values for
	// desired operation.
	// we explicitly set EN=1 (enable), SECRC=1 (strip CRC; the grade script
	// expects this)
	// we implicitly set LPE=0, LBM=0, BAM=0, BSIZE=00, and more..
	SET_BIT(RCTL, RCTL_EN_BITOFF);
	SET_BIT(RCTL, RCTL_SECRC_BITOFF);
	SET_BIT(RCTL, RCTL_BAM_BITOFF);
	// SET_BIT(RCTL, RCTL_UPE_BITOFF);
	SET_BIT(RCTL, RCTL_MPE_BITOFF);
	SET_BIT(RCTL, RCTL_SBP_BITOFF);
	CLEAR_BIT(RCTL, RCTL_LPE_BITOFF);
	CLEAR_BIT(RCTL, RCTL_LBM_BITOFF);
	CLEAR_BIT(RCTL, RCTL_LBM_BITOFF + 1);
	CLEAR_BIT(RCTL, RCTL_LPE_BITOFF);
}

int e1000_attach(struct pci_func *pcif) {
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

	e1000_tx_init();
	e1000_rx_init();

	e1000_initialized = 1;

	return 0;
}

// adds the given data to the txdesc_array and signals the e1000 on the TDT
// register to let the hardware know there's a new packet to be transmitted.
// 
// if the transmit queue is full, the packet is simply dropped for now.
// This is not a big problem because protocols should be resistant to
// packet loss.
// 
// Later we might want to return an error code, so that the transmitting
// process can retry if it wants to.
int e1000_transmit(unsigned char *data, size_t length) {

	// "Note that TDT is an index into the transmit descriptor array, not a
	// byte offset; the documentation isn't very clear about this."
	int index = TDT;

	assert (index >= 0);
	assert (index < TXDESC_ARRAY_SIZE);
	struct txdesc *desc = &txdesc_array[index];

	assert (BIT_IS_SET(desc->cmd, TXDESC_CMD_RS_BITOFF));
	assert (BIT_IS_SET(desc->cmd, TXDESC_CMD_EOP_BITOFF));

	if (length > sizeof(struct txbuf)) {
		cprintf("warning: dropping packet of length %d (too big)\n", length);
		return 0;
	}

	// We must check if a descriptor is in use before copying data into it; if
	// so, this means the queue is full and we must drop the packet.
	if (descriptor_is_in_use(desc)) {
		cprintf("warning: dropping packet (queue full)\n");
		return 0;
	}

	// there is space, so copy the data there
	copy_from_user(&txbuffers[index].data, data, length);
	desc->length = length;
	mark_descriptor_in_use(desc);

	// update the tail so the card knows there's a new packet to transmit
	TDT = (index + 1) % TXDESC_ARRAY_SIZE;

	return 0;
}

// takes a packet from the receive descriptors array and returns it via the
// 'buf' argument. 
// returns:
//   -E_NOT_READY if there's no packet to receive
//   -E_NO_MEM    if the buffer was too small
//   the size of the received packet otherwise
int e1000_receive(unsigned char *buf, size_t bufsize) {
	int index = (RDT + 1) % RXDESC_ARRAY_SIZE;
	struct rxdesc *desc = &rxdesc_array[index];

	// first we must check if the descriptor has been filled out by the
	// hardware. If not, the queue is empty; there's nothing to receive.
	if (!BIT_IS_SET(desc->status, RXDESC_STATUS_DD_BITOFF))
		return -E_NOT_READY;
	
	if (desc->length > bufsize)
		return -E_NO_MEM;
	
	copy_to_user(buf, &rxbuffers[index].data, desc->length);

	// the EOP bit should be set since every packet fits into one descriptor
	// (because we disallow jumbo frames)
	assert (BIT_IS_SET(desc->status, RXDESC_STATUS_EOP_BITOFF));

	// clear EOP and DD
	CLEAR_BIT(desc->status, RXDESC_STATUS_DD_BITOFF);
	CLEAR_BIT(desc->status, RXDESC_STATUS_EOP_BITOFF);

	// finally, update RDT
	RDT = index;

	return desc->length;
}
