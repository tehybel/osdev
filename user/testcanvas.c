#include <inc/lib.h>
#include <inc/graphics.h>


static void make_canvas_green() {
	int x, y;
	for (x = 0; x < canvas->width; x++) {
		for (y = 0; y < canvas->height; y++) {
			draw_pixel(x, y, COLOR_CRIMSON);
		}
	}
}

void umain(int argc, char **argv) {
	init_graphics();

	cprintf("testcanvas has spawned!\n");

	make_canvas_green();

}
