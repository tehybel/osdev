#include <inc/lib.h>
#include <inc/graphics.h>


/* 
 * any program which uses graphics must call this function first
 */
void init_graphics() {
	// receive the canvas from the display server
	Canvas *tmp = CANVAS_BASE - PGSIZE;
	ipc_recv(NULL, tmp, NULL);

	// immediately move the canvas into the global variable, so that the
	// IPC-shared page will never get messed with by others
	canvas = *tmp;

	canvas.raw_pixels = CANVAS_BASE;

	assert (canvas.width != 0);
	assert (canvas.height != 0);

	cprintf("init_graphics got the canvas size: %d\n", canvas.size);
}

inline int color(int r, int g, int b) {
	return 	((r & 0xff) << 16) | ((g & 0xff) << 8 ) | ((b & 0xff) << 0);
}

void draw_pixel(const int x, const int y, const int color) {
	if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) {
		return;
	}

	canvas.raw_pixels[canvas.width*y + x] = color;
}

// continuously receive events from the display server and process them
void event_loop(void (*process_event)(struct graphics_event *)) {
	while (1) {
		ipc_recv(NULL, SHARE_PAGE, NULL);
		struct graphics_event *ev = (struct graphics_event *) SHARE_PAGE;
		process_event(ev);
	}
}

// just colors the whole canvas
void color_canvas(int col) {
	int x, y;
	for (x = 0; x < canvas.width; x++) {
		for (y = 0; y < canvas.height; y++) {
			draw_pixel(x, y, col);
		}
	}
}

void draw_square(int x, int y, int side_length) {
	int i;
	for (i = 0; i < side_length; i++) {
		draw_pixel(x + i, y, COLOR_LIME);
		draw_pixel(x + i, y + side_length, COLOR_LIME);
		draw_pixel(x, y + i, COLOR_LIME);
		draw_pixel(x + side_length, y + i, COLOR_LIME);
	}
}
