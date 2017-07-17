#include <inc/lib.h>

#define LFB_BASE 0xff000000

uint8_t *lfb;
uint8_t *zbuffer;

size_t lfb_size;
size_t pitch;
size_t width;
size_t height;
size_t bpp;

static void init_lfb() {
	int r;
	if ((r = sys_map_lfb()))
		panic("could not initialize lfb.", r);
	
	lfb = (uint8_t *) LFB_BASE;


	// hardcode these for now. 
	// we should really get them from the mode_info struct in the kernel via a
	// syscall.
	width = 1024;
	height = 768;
	pitch = 4096; // bytes per horizontal line
	bpp = 32;
	lfb_size = width * height * bpp / 8;
}

static void init_zbuffer() {
	// TODO
}

int color(int r, int g, int b) {
	return 	((r & 0xff) << 16) | 
			((g & 0xff) << 8 ) | 
			((b & 0xff) << 0 );
}

#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

void draw_pixel(uint8_t *zbuffer, int x, int y, int color) {
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
			draw_pixel(lfb, i, j, c);
		}
	}

	c = color(0xff, 0xff, 0);

#define D(x, y) draw_pixel(lfb, x, y, c)

	D(5, 0);
	D(5, 1);
	D(5, 2);
	D(5, 3);
	D(5, 4);
	D(5, 5);
	D(5, 6);
	D(5, 7);
	D(5, 8);
	D(5, 9);


}

