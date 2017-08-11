#include <inc/lib.h>
#include <inc/graphics.h>


/* 
 * any program which uses graphics must call this function first
 */
void init_graphics() {
	int r;

	// allocate a page for the canvas
	if ((r = sys_page_alloc(0, CANVAS_BASE - PGSIZE, PTE_U | PTE_P | PTE_W)))
		panic("init_graphics sys_page_alloc: %e", r);

	canvas = CANVAS_BASE - PGSIZE;

	// receive the canvas from the display server
	ipc_recv(NULL, canvas, NULL);

	canvas->raw_pixels = CANVAS_BASE;

	cprintf("init_graphics got the canvas size: %d\n", canvas->size);
}

inline int color(int r, int g, int b) {
	return 	((r & 0xff) << 16) | ((g & 0xff) << 8 ) | ((b & 0xff) << 0);
}

void draw_pixel(const int x, const int y, const int color) {
	if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) 
		panic("invalid draw_pixel");

	canvas->raw_pixels[canvas->width*y + x] = color;
}
