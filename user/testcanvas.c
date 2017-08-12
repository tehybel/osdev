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

static void draw_square(int x, int y, int side_length) {
	int i;
	for (i = 0; i < side_length; i++) {
		draw_pixel(x + i, y, COLOR_LIME);
		draw_pixel(x + i, y + side_length, COLOR_LIME);
		draw_pixel(x, y + i, COLOR_LIME);
		draw_pixel(x + side_length, y + i, COLOR_LIME);
	}
}

static void process_event(struct graphics_event *ev) {
	if (ev->type == EVENT_MOUSE_CLICK) {
		draw_square(ev->d.emc.x, ev->d.emc.y, 20);
	}
}

// continuously receive events from the display server and process them
static void event_loop() {
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
