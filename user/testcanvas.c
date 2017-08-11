#include <inc/lib.h>
#include <inc/graphics.h>

#define SHARE_PAGE (CANVAS_BASE - PGSIZE*2)

static void color_canvas() {
	int x, y;
	for (x = 0; x < canvas->width; x++) {
		for (y = 0; y < canvas->height; y++) {
			draw_pixel(x, y, COLOR_CRIMSON);
		}
	}
}

static void process_event(struct graphics_event *ev) {
	cprintf("got an event: 0x%x\n", ev);
}

// continuously receive events from the display server and process them
static void event_loop() {
	int r;
	if ((r = sys_page_alloc(0, SHARE_PAGE, PTE_U | PTE_P | PTE_W)))
		panic("event_loop sys_page_alloc: %e", r);

	while (1) {
		ipc_recv(NULL, SHARE_PAGE, NULL);

		struct graphics_event *ev = (struct graphics_event *) SHARE_PAGE;
		process_event(ev);
	}
}

void umain(int argc, char **argv) {
	init_graphics();
	color_canvas();
	event_loop();
}
