#include <inc/lib.h>
#include <inc/graphics.h>

static void process_event(struct graphics_event *ev) {
	// do nothing for now.
}

void umain(int argc, char **argv) {
	init_graphics();
	color_canvas(COLOR_WHITE);
	event_loop(process_event);
}
