#include <inc/ns.h>
#include <inc/lib.h>

// these are hardcoded for now, based on QEMU's defaults; QEMU will provide a
// virtual router at 10.0.2.2 and give the guest an IP of 10.0.2.15.
#define IP "10.0.2.15"
#define MASK "255.255.255.0"
#define DEFAULT "10.0.2.2"

#define TIMER_INTERVAL 250

// Virtual address at which to receive page mappings containing client requests.
#define QUEUE_SIZE	20
#define REQVA		(0x0ffff000 - QUEUE_SIZE * PGSIZE)

/* timer.c */
void timer(envid_t ns_envid, uint32_t initial_to);

/* input.c */
void input(envid_t ns_envid);

/* output.c */
void output(envid_t ns_envid);

