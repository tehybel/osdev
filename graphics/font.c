#include <inc/lib.h>
#include <inc/graphics.h>

static void process_event(struct graphics_event *ev) {
	// do nothing for now.
}

// show an example of using a font
static void display_font() {
	draw_text("Lorem Ipsum", 10, 10, COLOR_BLACK, font_10x18);
}

void umain(int argc, char **argv) {
	init_graphics();
	color_canvas(COLOR_WHITE);
	display_font();
	event_loop(process_event);
}
