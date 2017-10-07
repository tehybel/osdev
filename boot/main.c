#include <inc/x86.h>
#include <inc/elf.h>

/**********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(boot.S and main.c) is the bootloader.  It should
 *    be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in boot.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 **********************************************************************/

#define SECTSIZE	512
#define ELFHDR		((struct Elf *) 0x10000) // scratch space

void readsect(void*, uint32_t);
void readseg(uint32_t, uint32_t, uint32_t);


/* write directly into video memory to print characters to the screen.
 * NOTE: You cannot print strings, because only .text is included in the raw
 * image, not the .rodata section. */
#define PRINT(c, i) do{*(int *) (0xB8000 + ((i)<<2)) = 0x07410700 | c;} while(0)


/* This is the base address at which we find the 8 ports needed to talk to the
 * ATA interface. It's 0x1f0 for the primary disk in (P)ATA. However it will
 * be different for SATA and really should be found via PCI..
 */ 

// ATA base:
#define IO_BASE 0x1f0

// SATA base for my netbook (found with "lspci -v" on Linux):
// #define IO_BASE 0x60b8

void
bootmain(void)
{
	struct Proghdr *ph, *eph;

	// read 1st page off disk
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// is this a valid ELF?
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;

	// load each program segment (ignores ph flags)
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		// p_pa is the load address of this segment (as well
		// as the physical address)
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);

	// call the entry point from the ELF header
	// note: does not return!
	((void (*)(void)) (ELFHDR->e_entry))();

bad:
	while (1) ;
}

// Read 'count' bytes at 'offset' from kernel into physical address 'pa'.
// Might copy more than asked
void
readseg(uint32_t pa, uint32_t count, uint32_t offset)
{
	uint32_t end_pa;

	end_pa = pa + count;

	// round down to sector boundary
	pa &= ~(SECTSIZE - 1);

	// translate from bytes to sectors, and kernel starts at sector 1
	offset = (offset / SECTSIZE) + 1;

	// If this is too slow, we could read lots of sectors at a time.
	// We'd write more to memory than asked, but it doesn't matter --
	// we load in increasing order.
	while (pa < end_pa) {
		// Since we haven't enabled paging yet and we're using
		// an identity segment mapping (see boot.S), we can
		// use physical addresses directly.  This won't be the
		// case once JOS enables the MMU.
		readsect((uint8_t*) pa, offset);
		pa += SECTSIZE;
		offset++;
	}
}

#define DELAY() inb(0x84)

#define ATA_BUSY (1<<7)
#define ATA_READY (1<<6)

#define MAX_TIMEOUTS 10

void
waitdisk(void)
{
	uint8_t status;

	while (1) {
		status = inb(IO_BASE + 7);
		if (status == 0xff) {
			PRINT('F', 15); 
		}

		else if ((status & ATA_BUSY) == 0 && (status & ATA_READY)) {
			// drive is ready
			return;
		}
	}

	/*
	// drive is probably floating, but make 100% sure
	PRINT('0', 11);

	outb(0x1f4, 0x55);
	outb(0x1f5, 0xAA);
	uint8_t byte0 = inb(0x1f4);
	outb(0x80, 0x55);
	uint8_t byte1 = inb(0x1f5);
	if (byte0 != 0x55 || byte1 != 0xAA) {
		// definitely a floating bus.
		PRINT('1', 12);
	} else {
		PRINT('2', 13);
	}
	*/
}

void
readsect(void *dst, uint32_t offset)
{
	// wait for disk to be ready
	PRINT('A', 0);
	waitdisk();
	PRINT('B', 1);

	outb(IO_BASE + 2, 1);		// count = 1
	outb(IO_BASE + 3, offset);
	outb(IO_BASE + 4, offset >> 8);
	outb(IO_BASE + 5, offset >> 16);
	outb(IO_BASE + 6, (offset >> 24) | 0xE0);
	outb(IO_BASE + 7, 0x20);	// cmd 0x20 - read sectors

	// wait for disk to be ready
	waitdisk();

	// read a sector
	insl(IO_BASE + 0, dst, SECTSIZE/4);
}

