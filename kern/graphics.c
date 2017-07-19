#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/graphics.h>

extern void *realmode_gdt;
extern int do_init_graphics();

void io_event_put(struct io_event *event) {
	assert (io_events_queue_cursize >= 0);
	assert (io_events_queue_cursize <= IO_EVENTS_QUEUE_SIZE);

	if (io_events_queue_cursize == IO_EVENTS_QUEUE_SIZE) {
		cprintf("warning: io events queue overflowed\n");
		return;
	}

	io_events_queue[io_events_queue_cursize++] = *event;
}

static void print_mode_info(struct vbe_mode_info *m) {
	cprintf("height: %d\n", m->height);
	cprintf("width:  %d\n", m->width);
	cprintf("pitch:  %d\n", m->pitch);
	cprintf("bpp:    %d\n", m->bpp);
	cprintf("LFB:    0x%x\n", m->framebuffer);

}

void init_graphics() {
	
	// since we're switching to real mode, we need to do so from code which
	// resides at a place which is 1:1 mapped, which should currently be true
	// for the first 1MB of code.
	memcpy((void *) 0x8000, do_init_graphics, 0x1000);

	memcpy((void *) 0x9000, &realmode_gdt, 0x1000);

	cprintf("Setting video mode..\n");

	int (*fptr)() = (void *) 0x8000;
	int res = fptr();

	if (!res) {
		have_graphics = 0;
		cprintf("Failed to set a video mode.\n");
		return;
	}

	// if do_int_graphics succeeded, a mode info struct now resides at 0xd000
	mode_info = *(struct vbe_mode_info *) 0xd000;

	have_graphics = 1;
	lfb_size = (mode_info.width + mode_info.pitch) * mode_info.height *
		mode_info.bpp / 8;

	cprintf("Managed to set video mode!\n");
	print_mode_info(&mode_info);
}

