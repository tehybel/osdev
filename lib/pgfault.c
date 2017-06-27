// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
void (*_pgfault_handler)(struct UTrapframe *utf);

// Set the page fault handler function.
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	if (_pgfault_handler == 0) {
		// this is the first time through, so we must allocate an exception
		// stack.
		if (sys_page_alloc(0, (void *) UXSTACKBASE, PTE_U | PTE_P | PTE_W))
			panic("sys_page_alloc failed");

		// also tell the kernel to use our handler
		if (sys_env_set_pgfault_upcall(0, _pgfault_upcall))
			panic("sys_env_set_pgfault_upcall failed");
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
