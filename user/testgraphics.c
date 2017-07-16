#include <inc/lib.h>

#define LFB_BASE 0xff000000

/* this process should be special; it should have access to the LFB directly.
 * So we need a syscall or such to map the LFB into this process's address
 * space. 

 * then we need to allocate a big zbuffer.

 * we also need to know certain information from the mode buffer, so being
 * able to retrieve that would be useful. We'll need another syscall for that.
 * */

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

	if (x < 0 || y < 0 || x >= width || y >= height)
		return;
	
	uint32_t offset = y * pitch + x * bpp / 8;

	zbuffer[offset+0] = BLUE(color);
	zbuffer[offset+1] = GREEN(color);
	zbuffer[offset+2] = RED(color);
}


void umain(int argc, char **argv) {
	int i, j;

	cprintf("graphics environment started!\n");

	init_lfb();
	init_zbuffer();


	for (i = 0; i < width; i++) {
		for (j = 0; j < height; j++) {
			int c = color(i, j, 0xff);
			draw_pixel(lfb, i, j, c);
		}
	}



}

