#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/graphics.h>

extern void *realmode_gdt;
extern int _get_video_mode();
extern int _set_video_mode();

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
	cprintf("height: %d, ", m->height);
	cprintf("width:  %d, ", m->width);
	cprintf("bpp:    %d\n", m->bpp);
	cprintf("pitch:  %d, ", m->pitch);
	cprintf("LFB:    0x%x\n", m->framebuffer);

}

bool graphics_enabled() {
	return 1;
}

void init_graphics() {
	unsigned int mode_offset;
	int done;
	
	have_graphics = 0;

	if (!graphics_enabled()) {
		cprintf("Graphics are not enabled.\n");
		return;
	}
	
	// since we're switching to real mode, we need to do so from code which
	// resides at a place which is 1:1 mapped, which should currently be true
	// for the first 1MB of code.
	memcpy((void *) 0x8000, _get_video_mode, 0x1000);
	memcpy((void *) 0x9000, &realmode_gdt, 0x1000);
	int (*get_video_mode)(int) = (void *) 0x8000;

	// enumerate the different video modes
	for (mode_offset = 0, done = 0; get_video_mode(mode_offset); mode_offset += 2) {

		// if get_video_mode succeeded, a mode info struct now resides at 0xd000
		mode_info = *(struct vbe_mode_info *) 0xd000;

		// cprintf("Video mode %d: %dx%dx%d\n", mode_offset, mode_info.width,
		// 		mode_info.height, mode_info.bpp);

		if (mode_info.width == GRAPHICS_WIDTH && 
			mode_info.height == GRAPHICS_HEIGHT && 
			mode_info.bpp == GRAPHICS_BPP) {

			have_graphics = 1;
			break;
		}
	}

	if (!have_graphics) {
		cprintf("Failed to find a valid video mode.\n");
		return;
	} 

	cprintf("Found a valid video mode:\n");
	print_mode_info(&mode_info);

	memcpy((void *) 0x8000, _set_video_mode, 0x1000);
	memcpy((void *) 0x9000, &realmode_gdt, 0x1000);
	int (*set_video_mode)(int) = (void *) 0x8000;

	if (!set_video_mode(mode_offset)) {
		cprintf("Could not set the video mode.\n");
		have_graphics = 0;
		return;
	}
	cprintf("Successfully set the video mode.\n");

	lfb_size = (mode_info.width + mode_info.pitch) * mode_info.height *
				mode_info.bpp / 8;
}

