#include <inc/lib.h>
#include <kern/graphics.h>

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

uint32_t *lfb;
uint32_t *zbuffer;

size_t lfb_size;
size_t pitch;
size_t width;
size_t height;
size_t bpp;

int cursor_x, cursor_y;


#define NUM_EVENTS 100
struct io_event events[NUM_EVENTS];

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

static int color(int r, int g, int b) {
	return 	((r & 0xff) << 16) | 
			((g & 0xff) << 8 ) | 
			((b & 0xff) << 0 );
}

#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

#define COLOR_WHITE color(0xff, 0xff, 0xff)

static inline void draw_pixel(const int x, const int y, const int color) {
	// For 32-bit modes, each pixel value is 0x00RRGGBB in little endian

	if (x < 0 || y < 0 || x >= width || y >= height) {
		return;
	}

	// if bpp isn't 32, we can't use a uint32_t*.
	assert (bpp == 32);
	uint32_t *p = (uint32_t *) zbuffer;
	p[y*((width+pitch)/4)  + x] = color;
}

/* draws a rectangle from upper left corner (x1, y1) to lower right corner
 * (x2, y2) */
static void draw_rectangle(const int x1, const int y1, 
						   const int x2, const int y2, const int color) {
	assert (x2 >= x1);
	assert (y2 >= y1);
	assert (x1 >= 0);
	assert (y1 >= 0);

	int times = (width + pitch)/4;

	int x, y;
	for (x = x1; x < x2; x++) {
		for (y = y1; y < y2; y++) {
			// we inline draw_pixel here for speed
			uint32_t *p = (uint32_t *) zbuffer;
			p[times*y  + x] = color;
		}
	}
}

static void refresh_screen() {
	memcpy(lfb, zbuffer, lfb_size);
}

static void draw_background() {
	draw_rectangle(0, 0, width, height, color(0xa0, 0xa0, 0xa0));
}

static void draw_windows() {
	// not implemented yet
}

static void draw_cursor() {
	#define CURSOR_SIZE 20
	int x1 = MAX(0, cursor_x - CURSOR_SIZE/2);
	int x2 = MIN(cursor_x + CURSOR_SIZE/2, width);
	int y1 = MAX(0, cursor_y - CURSOR_SIZE/2);
	int y2 = MIN(cursor_y + CURSOR_SIZE/2, height);
	draw_rectangle(x1, y1, x2, y2, COLOR_WHITE);
}

static void process_event(struct io_event *e) {
	switch (e->type) {
	case MOUSE_MOVE:
		cursor_x = e->data[0];
		cursor_y = e->data[1];
		break;

	case MOUSE_CLICK:
		// TODO
		break;
	
	case KEYBOARD_KEY:
		// TODO
		break;

	default:
		break;

	}
}

static int process_events() {
	int n = sys_get_io_events(&events, NUM_EVENTS);
	int i;

	for (i = 0; i < n; i++) {
		process_event(&events[i]);
	}

	return n;
}

void umain(int argc, char **argv) {

	cprintf("graphics environment started!\n");

	init_lfb();
	init_zbuffer();

	while (1) {
		if (!process_events())
			continue;

		draw_background(); // slow
		draw_windows();
		draw_cursor();
		refresh_screen(); // slow
	}

}
