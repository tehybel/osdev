// Search for and parse the multiprocessor configuration table
// See http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include <inc/types.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/env.h>
#include <kern/cpu.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

struct CpuInfo cpus[NCPU];
struct CpuInfo *bootcpu;
int ismp;
int ncpu;

// Per-CPU kernel stacks
unsigned char percpu_kstacks[NCPU][KSTKSIZE]
__attribute__ ((aligned(PGSIZE)));


// See MultiProcessor Specification Version 1.[14]

struct mp {             // floating pointer [MP 4.1]
	uint8_t signature[4];           // "_MP_"
	physaddr_t physaddr;            // phys addr of MP config table
	uint8_t length;                 // 1
	uint8_t specrev;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t type;                   // MP system config type
	uint8_t imcrp;
	uint8_t reserved[3];
} __attribute__((__packed__));

struct mpconf {         // configuration table header [MP 4.2]
	uint8_t signature[4];           // "PCMP"
	uint16_t length;                // total table length
	uint8_t version;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t product[20];            // product id
	physaddr_t oemtable;            // OEM table pointer
	uint16_t oemlength;             // OEM table length
	uint16_t entry;                 // entry count
	physaddr_t lapicaddr;           // address of local APIC
	uint16_t xlength;               // extended table length
	uint8_t xchecksum;              // extended table checksum
	uint8_t reserved;
	uint8_t entries[0];             // table entries
} __attribute__((__packed__));

struct mpproc {         // processor table entry [MP 4.3.1]
	uint8_t type;                   // entry type (0)
	uint8_t apicid;                 // local APIC id
	uint8_t version;                // local APIC version
	uint8_t flags;                  // CPU flags
	uint8_t signature[4];           // CPU signature
	uint32_t feature;               // feature flags from CPUID instruction
	uint8_t reserved[8];
} __attribute__((__packed__));

// mpproc flags
#define MPPROC_BOOT 0x02                // This mpproc is the bootstrap processor

// Table entry types
#define MPPROC    0x00  // One per processor
#define MPBUS     0x01  // One per bus
#define MPIOAPIC  0x02  // One per I/O APIC
#define MPIOINTR  0x03  // One per bus interrupt source
#define MPLINTR   0x04  // One per system interrupt source


struct RSDP_descriptor {
	char signature[8];
	uint8_t Checksum;
	char OEMID[6];
	uint8_t revision;
	uint32_t rsdt_address;
	
	// the next fields are only valid if revision >= 2;
	// see http://wiki.osdev.org/RSDP
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t ExtendedChecksum;
	uint8_t reserved[3];
} __attribute__ ((packed));


// standard ACPI table header
struct SDT_header {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char OEM_id[6];
	char OEM_table_id[8];
	uint32_t OEM_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
};

struct RSDT {
	struct SDT_header h;
	uint32_t sdt_ptrs[32];
};

struct MADT {
	struct SDT_header h;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t apic_headers[256]; // variable-length
};

struct APIC_header {
	uint8_t type;
	uint8_t length;
};



static uint8_t
sum(void *addr, int len) {
	int i, sum;

	sum = 0;
	for (i = 0; i < len; i++)
		sum += ((uint8_t *)addr)[i];
	return sum;
}

static void print_mp(struct mp *mp) {
	cprintf("sig: %c %c %c %c\n", mp->signature[0], mp->signature[1],
			mp->signature[2], mp->signature[3]); 
	cprintf("physaddr: 0x%x\n", mp->physaddr);
	cprintf("length: %d\n", mp->length);
	cprintf("specrev: %d\n", mp->specrev);
	cprintf("checksum: %d\n", mp->checksum);
	cprintf("type: %d\n", mp->type);
	cprintf("imcrp: %d\n", mp->imcrp);
	cprintf("reserved: %d\n", mp->reserved[0]);
	cprintf("reserved: %d\n", mp->reserved[1]);
	cprintf("reserved: %d\n", mp->reserved[2]);
}

// Look for an MP structure in the len bytes at physical address addr.
static struct mp *
mpsearch1(physaddr_t a, int len)
{
	struct mp *mp = KADDR(a), *end = KADDR(a + len);

	for (; mp < end; mp++) {
		if (memcmp(mp->signature, "_MP_", 4) == 0) {
			uint8_t checksum = sum(mp, sizeof(*mp));
			if (checksum == 0) {
				return mp;
			} 
		}
	}
	
	return NULL;
}

// Search for the MP Floating Pointer Structure, which according to
// [MP 4] is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) if there is no EBDA, in the last KB of system base memory;
// 3) in the BIOS ROM between 0xF0000 and 0xFFFFF.
static struct mp *
mpsearch(void)
{
	uint8_t *bda;
	uint32_t p;
	struct mp *mp;

	static_assert(sizeof(*mp) == 16);

	// The BIOS data area lives in 16-bit segment 0x40.
	bda = (uint8_t *) KADDR(0x40 << 4);

	// [MP 4] The 16-bit segment of the EBDA is in the two bytes
	// starting at byte 0x0E of the BDA.  0 if not present.
	if ((p = *(uint16_t *) (bda + 0x0E))) {
		p <<= 4;	// Translate from segment to PA
		if ((mp = mpsearch1(p, 1024)))
			return mp;
	} else {
		// The size of base memory, in KB is in the two bytes
		// starting at 0x13 of the BDA.
		p = *(uint16_t *) (bda + 0x13) * 1024;
		if ((mp = mpsearch1(p - 1024, 1024)))
			return mp;
	}
	if ((mp = mpsearch1(0xF0000, 0x10000)))
		return mp;
	
	return mpsearch1(0, 0x1000000);
}

// Search for an MP configuration table.  For now, don't accept the
// default configurations (physaddr == 0).
// Check for the correct signature, checksum, and version.
static struct mpconf *
mpconfig(struct mp **pmp)
{
	struct mpconf *conf;
	struct mp *mp;

	if ((mp = mpsearch()) == 0) {
		return NULL;
	}
	if (mp->physaddr == 0 || mp->type != 0) {
		cprintf("SMP: Default configurations not implemented\n");
		return NULL;
	}
	conf = (struct mpconf *) KADDR(mp->physaddr);
	if (memcmp(conf, "PCMP", 4) != 0) {
		cprintf("SMP: Incorrect MP configuration table signature\n");
		return NULL;
	}
	if (sum(conf, conf->length) != 0) {
		cprintf("SMP: Bad MP configuration checksum\n");
		return NULL;
	}
	if (conf->version != 1 && conf->version != 4) {
		cprintf("SMP: Unsupported MP version %d\n", conf->version);
		return NULL;
	}
	if ((sum((uint8_t *)conf + conf->length, conf->xlength) + conf->xchecksum) & 0xff) {
		cprintf("SMP: Bad MP configuration extended checksum\n");
		return NULL;
	}
	*pmp = mp;
	return conf;
}

bool init_mp_via_mpconfig() {
	struct mp *mp;
	struct mpconf *conf;
	struct mpproc *proc;
	uint8_t *p;
	unsigned int i;

	bootcpu = &cpus[0];
	if ((conf = mpconfig(&mp)) == 0) {
		return 0;
	}
	ismp = 1;
	lapicaddr = conf->lapicaddr;

	for (p = conf->entries, i = 0; i < conf->entry; i++) {
		switch (*p) {
		case MPPROC:
			proc = (struct mpproc *)p;
			if (proc->flags & MPPROC_BOOT)
				bootcpu = &cpus[ncpu];
			if (ncpu < NCPU) {
				cpus[ncpu].cpu_id = ncpu;
				ncpu++;
			} else {
				cprintf("SMP: too many CPUs, CPU %d disabled\n", proc->apicid);
			}
			p += sizeof(struct mpproc);
			continue;
		case MPBUS:
		case MPIOAPIC:
		case MPIOINTR:
		case MPLINTR:
			p += 8;
			continue;
		default:
			cprintf("mpinit: unknown config type %x\n", *p);
			ismp = 0;
			i = conf->entry;
		}
	}

	bootcpu->cpu_status = CPU_STARTED;
	if (!ismp) {
		// Didn't like what we found; fall back to no MP.
		ncpu = 1;
		lapicaddr = 0;
		cprintf("SMP: configuration not found, SMP disabled\n");
		return 1;
	}
	cprintf("SMP: CPU %d found %d CPU(s)\n", bootcpu->cpu_id,  ncpu);

	if (mp->imcrp) {
		// [MP 3.2.6.1] If the hardware implements PIC mode,
		// switch to getting interrupts from the LAPIC.
		cprintf("SMP: Setting IMCR to switch from PIC mode to symmetric I/O mode\n");
		outb(0x22, 0x70);   // Select IMCR
		outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
	}

	return 1;
}

static void phys_memcpy_new(void *dst_va, physaddr_t src_pa, size_t count) {
	cprintf("copying %d bytes from PA 0x%x to VA 0x%x", count, src_pa,
	dst_va);
	
	void *src_va = (void *) src_pa;

	pte_t *pte = pgdir_walk(kern_pgdir, src_va, 1);
	pte_t old_pte = *pte;

	*pte = ROUNDDOWN(src_pa, PGSIZE) | PTE_P;
	invlpg(ROUNDDOWN(src_va, PGSIZE));
	invlpg(src_va);
	memcpy(dst_va, src_va, count);

	*pte = old_pte;
	invlpg(ROUNDDOWN(src_va, PGSIZE));
	invlpg(src_va);
}

static void phys_memcpy(void *dst_va, physaddr_t src_pa, size_t count) {
	// make sure that the physical address is mapped to a virtual one before
	// copying
	// we really should find an empty va here by walking over the pgdir.. TODO
	void *tmp = (void *) 0x20000000;

	size_t offset = PGOFF(src_pa);

	assert (offset <= PGOFF(src_pa + count));
	assert (count <= PGSIZE);
	// otherwise we must map in two pages, not just one

	pte_t *pte = pgdir_walk(kern_pgdir, tmp, 1);

	invlpg(tmp);
	*pte = ROUNDDOWN(src_pa, PGSIZE) | PTE_P;
	memcpy(dst_va, tmp + offset, count);
	*pte = 0;
	invlpg(tmp);
}

struct RSDP_descriptor *search_for_rsdp(uint32_t base, size_t length) {
	uint32_t ptr;
	for (ptr = base; ptr < base + length; ptr += 0x10) {
		if (!memcmp((void *) ptr, "RSD PTR ", 8)) {
			if (sum((void *) ptr, sizeof(struct RSDP_descriptor)) == 0) {
				return (struct RSDP_descriptor *) ptr;
			} 
		}
	}
	return NULL;
}

// based on http://wiki.osdev.org/RSDP
struct RSDP_descriptor *find_rsdp() {
	// The RSDP is either located within the first 1 KB of the EBDA (Extended
	// BIOS Data Area), or in the memory region from 0x000E0000 to 0x000FFFFF
	// (the main BIOS area below 1 MB).

	struct RSDP_descriptor *result;
	uint32_t ptr;
	uint8_t *bda = (uint8_t *) KADDR(0x40 << 4);

	if ((ptr = *(uint16_t *) (bda + 0x0E))) {
		// the EBDA ptr is valid, so search there
		if ((result = search_for_rsdp((uint32_t) KADDR(ptr), 1024)))
			return result;
	}

	// if we got nothing so far, look in the main BIOS area
	return search_for_rsdp((uint32_t) KADDR(0xe0000), 0x20000);
}

physaddr_t find_table(struct RSDT *rsdt, char *signature, size_t ptr_size) {
	int i;
	size_t num_entries = (rsdt->h.length - sizeof(rsdt->h)) / ptr_size;
	for (i = 0; i < num_entries; i++) {
		physaddr_t sdt_ptr = *(physaddr_t *)(((char *) rsdt->sdt_ptrs) + (ptr_size*i));
		struct SDT_header header;
		phys_memcpy(&header, sdt_ptr, sizeof(header));
		if (!strncmp(header.signature, signature, 4))
			return sdt_ptr;
	}
	return 0;
}

static void parse_madt(physaddr_t madt_pa) {
	struct MADT madt;
	phys_memcpy(&madt, madt_pa, sizeof(madt));
	assert (madt.h.length <= sizeof(struct MADT));

	lapicaddr = madt.lapic_addr;

	uint8_t *ptr = (void *) &madt.apic_headers;
	uint8_t *end = (uint8_t *) &madt + madt.h.length;
	while (ptr < end) {
		struct APIC_header *header = (void *) ptr;
		switch (header->type) {

		case 0: // Processor Local APIC
			if (ncpu < NCPU) {
				cpus[ncpu].cpu_id = ncpu;
				ncpu++;
			}
			else {
				cprintf("SMP: too many CPUs!\n");
			}

			break;

		default:
			// cprintf("unhandled APIC_header type: 0x%x\n", header->type);
			break;
		}
		ptr += header->length;
	}

	// according to the ACPI spec, "platform firmware should list the boot
	// processor as the first processor entry in the MADT"
	bootcpu = &cpus[0];
	bootcpu->cpu_status = CPU_STARTED;

	cprintf("Discovered %d CPU(s) via the MADT\n", ncpu);
}

bool init_mp_via_acpi() {
	physaddr_t madt;
	struct RSDT rsdt = {0};
	uint32_t rsdt_address;
	size_t ptr_size;

	// first we must find the RSDP (Root System Description Pointer)
	struct RSDP_descriptor *rsdp = find_rsdp();
	if (rsdp == NULL) {
		cprintf("could not find RSDP\n");
		return 0;
	}
	if (strncmp(rsdp->signature, "RSD PTR ", 8)) {
		cprintf("Got an invalid RSDP!\n");
		return 0;
	}

	cprintf("Revision is %d\n", rsdp->revision);
	if (rsdp->revision < 2) {
		// this is the case on old real hardware and QEMU
		// we must use the RSDT
		rsdt_address = rsdp->rsdt_address;
		ptr_size = 4;
	}
	else {
		// this is the case on VirtualBox
		// we must prefer the XSDT over the RSDT
		rsdt_address = rsdp->xsdt_address;
		ptr_size = 8;
	}

	cprintf("The RSDT should be at 0x%x\n", rsdt_address);

	// grab a copy of the RSDT from physical memory.
	// we cannot just use KADDR() here because the RSDT can reside at places
	// which aren't in our page table

	// first grab the header, which has the length
	phys_memcpy(&rsdt, rsdt_address, sizeof(struct SDT_header));
	if (strncmp(rsdt.h.signature, "RSDT", 4) != 0 &&
		strncmp(rsdt.h.signature, "XSDT", 4) != 0) {
		cprintf("Got an invalid RSDT! '%s'\n", rsdt.h.signature);
		return 0;
	}

	// then copy all of it
	assert (rsdt.h.length <= sizeof(rsdt));
	size_t length = MIN(rsdt.h.length, sizeof(rsdt));
	phys_memcpy(&rsdt, rsdt_address, length);

	// the RSDT basically points to a bunch of other tables; one of these is
	// the MADT (Multiple APIC Description Table) which lets us find the
	// LAPIC, so find the MADT.
	if (!(madt = find_table(&rsdt, "APIC", ptr_size))) {
		cprintf("could not find the MADT\n");
		return 0;
	} 

	// figure out the address of the LAPIC and enumerate the CPUs
	parse_madt(madt);

	return 1;
}

// mp_init retrieves information about the system related to multiprocessing,
// such as the total number of CPUs, by reading 'conf' which is a table filled
// out by the BIOS.
void init_multiprocessing() {
	// prefer to use the MP table provided by most BIOSes
	if (init_mp_via_mpconfig()) {
		return;
	}

	// if that fails (which it does on my netbook) then try to find the same
	// information via ACPI
	if (init_mp_via_acpi()) {
		return;
	}

	cprintf("warning: multiprocessing failed, both via mpconfig and ACPI.\n");
}
