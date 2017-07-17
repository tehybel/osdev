#include <inc/lib.h>

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

uint32_t *lfb;
uint32_t *zbuffer;

size_t lfb_size;
size_t pitch;
size_t width;
size_t height;
size_t bpp;

static void init_lfb() {
	int r;
	if ((r = sys_map_lfb()))
		panic("could not initialize lfb.", r);
	
	lfb = (uint32_t *) LFB_BASE;

	// hardcode these for now. 
	// we should really get them from the mode_info struct in the kernel via a
	// syscall.
	width = 1024;
	height = 768;
	pitch = 4096; // bytes per horizontal line
	bpp = 32;
	lfb_size = (width + pitch) * height * bpp / 8;
}

static void init_zbuffer() {
	uint32_t offset;
	int r;

	assert (lfb_size);

	for (offset = 0; offset < lfb_size; offset += PGSIZE) {
		void *va = (void *) ZBUFFER_BASE + offset;
		if ((r = sys_page_alloc(0, va, PTE_P | PTE_U | PTE_W)))
			panic("failed to allocate zbuffer: %e\n", r);
	}

	zbuffer = (uint32_t *) ZBUFFER_BASE;
}

int color(int r, int g, int b) {
	return 	((r & 0xff) << 16) | 
			((g & 0xff) << 8 ) | 
			((b & 0xff) << 0 );
}

#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

void draw_pixel(int x, int y, int color) {
	// For 32-bit modes, each pixel value is 0x00RRGGBB in little endian

	if (x < 0 || y < 0 || x >= width || y >= height) {
		cprintf("oob write in draw_pixel: %d %d\n", x, y);
		return;
	}

	// if bpp isn't 32, we can't use a uint32_t*.
	assert (bpp == 32);
	uint32_t *p = (uint32_t *) zbuffer;
	p[y*((width+pitch)/4)  + x] = color;
}

/* draws a rectangle from upper left corner (x1, y1) to lower right corner
 * (x2, y2) */
void draw_rectangle(int x1, int y1, int x2, int y2, int color) {
	assert (x2 >= x1);
	assert (y2 >= y1);

	int x, y;
	for (x = x1; x < x2; x++) {
		for (y = y1; y < y2; y++) {
			draw_pixel(x, y, color);
		}
	}
}

void refresh_screen() {
	memcpy(lfb, zbuffer, lfb_size);
}

void umain(int argc, char **argv) {
	int i, j, c;

	cprintf("graphics environment started!\n");

	init_lfb();
	init_zbuffer();

	for (i = 0; i < width; i++) {
		for (j = 0; j < height; j++) {
			int r = 256*i/width;
			int g = 256*j/height;
			int c = color(r, g, 0xff);
			draw_pixel(i, j, c);
		}
	}

	c = color(0xff, 0xff, 0);
	draw_rectangle(10, 10, 50, 100, c);

	c = color(0x20, 0x40, 0xe0);
	draw_rectangle(300, 300, 500, 500, c);

	refresh_screen();
}

