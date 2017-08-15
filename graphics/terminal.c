#include <inc/lib.h>
#include <inc/graphics.h>

static void process_event(struct graphics_event *ev) {
	char text[2];

	// ignore mouse events for now.
	if (ev->type != EVENT_KEY_PRESS)
		return;
	
	text[0] = ev->d.ekp.ch;
	text[1] = '\0';

	draw_text(text, 10, 10, COLOR_WHITE, font_10x18);

}

void umain(int argc, char **argv) {
	init_graphics();
	color_canvas(COLOR_BLACK);
	event_loop(process_event);
}
