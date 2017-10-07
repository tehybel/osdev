/*
 * Minimal PIO-based (non-interrupt-driven) IDE driver code.
 * For information about what all this IDE/ATA magic means,
 * see the materials available on the class references page.
 */

#include "fs.h"
#include <inc/x86.h>

#define IDE_BSY		0x80
#define IDE_DRDY	0x40
#define IDE_DF		0x20
#define IDE_ERR		0x01

// ATA base:
// #define IO_BASE 0x1f0

// SATA base (for my netbook, found with "lspci -v" on Linux):
// TODO: find this dynamically with PCI
#define IO_BASE 0x60b8

static int diskno = 1;

static int
ide_wait_ready(bool check_error)
{
	int r;

	while (1) {
		r = inb(IO_BASE + 7);

		if (r == 0xff) 
			panic("floating bus (no IDE drives)\n");

 		if ((r & (IDE_BSY|IDE_DRDY)) == IDE_DRDY)
			break;
	}

	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
		return -1;

	return 0;
}

bool
ide_probe_disk1(void)
{
	int r, x;

	// wait for Device 0 to be ready
	ide_wait_ready(0);

	// switch to Device 1
	outb(IO_BASE + 6, 0xE0 | (1<<4));

	// check for Device 1 to be ready for a while
	for (x = 0;
	     x < 1000 && ((r = inb(IO_BASE + 7)) & (IDE_BSY|IDE_DF|IDE_ERR)) != 0;
	     x++)
		/* do nothing */;

	// switch back to Device 0
	outb(IO_BASE + 6, 0xE0 | (0<<4));

	return (x < 1000);
}

void
ide_set_disk(int d)
{
	if (d != 0 && d != 1)
		panic("bad disk number");
	diskno = d;
}


int
ide_read(uint32_t secno, void *dst, size_t nsecs)
{
	int r;

	assert(nsecs <= 256);

	ide_wait_ready(0);

	outb(IO_BASE + 2, nsecs);
	outb(IO_BASE + 3, secno & 0xFF);
	outb(IO_BASE + 4, (secno >> 8) & 0xFF);
	outb(IO_BASE + 5, (secno >> 16) & 0xFF);
	outb(IO_BASE + 6, 0xE0 | ((diskno&1)<<4) | ((secno>>24)&0x0F));
	outb(IO_BASE + 7, 0x20);	// CMD 0x20 means read sector

	for (; nsecs > 0; nsecs--, dst += SECTSIZE) {
		if ((r = ide_wait_ready(1)) < 0)
			return r;
		insl(IO_BASE, dst, SECTSIZE/4);
	}

	return 0;
}

int
ide_write(uint32_t secno, const void *src, size_t nsecs)
{
	int r;

	assert(nsecs <= 256);

	ide_wait_ready(0);

	outb(IO_BASE + 2, nsecs);
	outb(IO_BASE + 3, secno & 0xFF);
	outb(IO_BASE + 4, (secno >> 8) & 0xFF);
	outb(IO_BASE + 5, (secno >> 16) & 0xFF);
	outb(IO_BASE + 6, 0xE0 | ((diskno&1)<<4) | ((secno>>24)&0x0F));
	outb(IO_BASE + 7, 0x30);	// CMD 0x30 means write sector

	for (; nsecs > 0; nsecs--, src += SECTSIZE) {
		if ((r = ide_wait_ready(1)) < 0)
			return r;
		outsl(IO_BASE, src, SECTSIZE/4);
	}

	return 0;
}

