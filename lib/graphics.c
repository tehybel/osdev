#include <inc/lib.h>
#include <inc/graphics.h>

Canvas canvas;

void init_graphics() {
	canvas.raw_pixels = CANVAS_BASE;
	canvas.size = ipc_recv(NULL, NULL, NULL);

	cprintf("init_graphics got the canvas size: %d\n", canvas.size);
}
