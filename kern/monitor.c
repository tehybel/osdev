// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "bt", "Display a backtrace", mon_backtrace },
	{ "dumpstack", "displays the setack", mon_dumpstack },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

static void print_stackframe(struct stackframe * sf) {
	struct Eipdebuginfo info;
	int i;

	debuginfo_eip(sf->eip, &info);

	cprintf("ebp=0x%08x, ", sf->ebp);
	cprintf("eip=0x%08x, ", sf->eip);
	cprintf("args={");
#define NUM_ARGS 3
	for (i = 0; i < NUM_ARGS; i++) {
		cprintf("0x%08x%s", sf->args[i], i == NUM_ARGS - 1 ? "" : ", ");
	}
#undef NUM_ARGS
	cprintf("} %.*s\n", info.eip_fn_namelen, info.eip_fn_name);

}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	struct stackframe *sf;
	for (sf = (struct stackframe *) read_ebp(); sf; sf = sf->ebp)
		print_stackframe(sf);

	return 0;
}

int mon_dumpstack(int argc, char **argv, struct Trapframe *tf)
{
	int i;
	for (i = 0; i < 20; i++) {
		cprintf("stack[%d]: 0x%x\n", i, ((uint32_t *)&i)[i]);
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline(" > ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
