#include <inc/lib.h>
#include <inc/graphics.h>
#include <kern/graphics.h>
#include <graphics/displayserver.h>


uint32_t *lfb;
uint32_t *zbuffer;

// size of the LFB *in bytes*
size_t lfb_size;

size_t pitch;
size_t width;
size_t height;
size_t bpp;

int cursor_x, cursor_y;

struct io_event events[NUM_EVENTS];

// used to share values with child processes
#define SHARE_PAGE (CANVAS_BASE - PGSIZE)

Application * applications_list = NULL;

struct graphics_event *events_queue = NULL;

static void alloc_share_page() {
	int r;
	if ((r = sys_page_alloc(0, SHARE_PAGE, PTE_U | PTE_P | PTE_W)))
		panic("sys_page_alloc: %e", r);
}

static void dealloc_share_page() {
	if (sys_page_unmap(0, SHARE_PAGE))
		panic("sys_page_unmap");
}

// shares some data via the SHARE_PAGE using IPC
static void share(int pid, void *data, size_t size) {
	alloc_share_page();
	memcpy(SHARE_PAGE, data, size);
	ipc_send(pid, 0, SHARE_PAGE, PTE_P | PTE_U | PTE_W);
	dealloc_share_page();
}

static int try_share(int pid, void *data, size_t size) {
	int r;
	alloc_share_page();
	memcpy(SHARE_PAGE, data, size);
	r = sys_ipc_try_send(pid, 0, SHARE_PAGE, PTE_P | PTE_U);
	dealloc_share_page();
	return r;
}

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

static inline void do_draw_pixel(const int x, const int y, const int color) {
	// For 32-bit modes, each pixel value is 0x00RRGGBB in little endian

	if (x < 0 || y < 0 || x >= width || y >= height) {
		return;
	}

	// if bpp isn't 32, we can't use a uint32_t*.
	assert (bpp == 32);
	uint32_t *p = (uint32_t *) zbuffer;

	if (p[y*((width+pitch)/4) + x] != color)
		p[y*((width+pitch)/4) + x] = color;
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
	for (y = y1; y < y2; y++) {
		int tmp = times*y;
		for (x = x1; x < x2; x++) {
			// we inline do_draw_pixel here for speed
			uint32_t *p = (uint32_t *) zbuffer;
			int index = tmp + x;

			if (p[index] != color)
				p[index] = color;
		}
	}
}

/*
	Walk over the zbuffer, checking if each byte was changed by comparing to
	the LFB. If a byte was changed, update it.

	We do this in a somewhat complicated way for performance; the efficiency
	of this function is critical for the frame rate of our graphics.
*/
static void refresh_screen() {
	int x, y, index, tmp;
	Pixel p;
		
	for (y = 0; y < height; y++) {
		tmp = y*((width+pitch)>>2);
		for (x = 0; x < width; x++) {
			index = tmp + x;
			p = zbuffer[index];
			if (lfb[index] != p)
				lfb[index] = p;
		}
	}
}

static void draw_background() {
	draw_rectangle(0, 0, width, height, color(0xa0, 0xa0, 0xa0));
}

static void draw_cursor() {
	int x1 = MAX(0, cursor_x);
	int x2 = MIN(cursor_x + CURSOR_SIZE, width);
	int y1 = MAX(0, cursor_y);
	int y2 = MIN(cursor_y + CURSOR_SIZE, height);
	draw_rectangle(x1, y1, x2, y2, COLOR_WHITE);
}

static Application *get_app_for_coordinate(int x, int y) {
	Application *app;
	for (app = applications_list; app; app = app->next) {
		if (x < app->window.x_pos)
			continue;
		if (y < app->window.y_pos)
			continue;
		if (x >= app->window.x_pos + app->window.width)
			continue;
		if (y >= app->window.y_pos + app->window.height)
			continue;

		return app;
	}

	return NULL;
}

static void add_event_to_queue(struct graphics_event *ev) {
	ev->next = events_queue;
	events_queue = ev;
}

static void handle_mouse_click(int button) {
	Application *app = get_app_for_coordinate(cursor_x, cursor_y);
	Window *w = &app->window;
	if (!w) return;

	struct graphics_event *ev = calloc(1, sizeof(struct graphics_event));
	if (!ev) panic("malloc event");

	ev->type = EVENT_MOUSE_CLICK;
	ev->d.emc.x = cursor_x;
	ev->d.emc.y = cursor_y;
	ev->recipient = app->pid;

	add_event_to_queue(ev);
}

static void process_event(struct io_event *e) {
	switch (e->type) {
	case MOUSE_MOVE:
		cursor_x = e->data[0];
		cursor_y = e->data[1];
		break;

	case MOUSE_CLICK:
		handle_mouse_click(e->data[0]);
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

static Canvas *alloc_canvas(Window *w) {
	size_t offset;
	static void *canvas_mem = CANVAS_BASE;

	Canvas *c = calloc(1, sizeof(Canvas));
	if (!c) panic("alloc_canvas");

	c->size = w->height * w->width * (bpp/8);
	c->height = w->height;
	c->width = w->width;
	c->raw_pixels = canvas_mem;

	// allocate all the pages consecutively
	for (offset = 0; offset < c->size; offset += PGSIZE) {
		if (sys_page_alloc(0, canvas_mem, PTE_U | PTE_P | PTE_W))
			panic("alloc_canvas sys_page_alloc");
		canvas_mem += PGSIZE;
	}

	assert (c->width != 0);
	assert (c->height != 0);

	return c;
}

static void init_window(Window *w, int x, int y, int height, int width) {
	w->x_pos = x;
	w->y_pos = y;

	w->height = height;
	w->width = width;

	w->canvas = alloc_canvas(w);
}

static void mark_perm (void *mem, size_t size, int perm) {
	size_t offset;
	for (offset = 0; offset < size; offset += PGSIZE) {
		if (sys_page_map(0, mem + offset, 0, mem + offset, perm))
			panic("mark_perm");
	}
}
static void mark_nonshared (void *mem, size_t size) {
	mark_perm(mem, size, PTE_U | PTE_P | PTE_W);
}

static void mark_shared (void *mem, size_t size) {
	mark_perm(mem, size, PTE_U | PTE_P | PTE_W | PTE_SHARE);
}

static void spawn_program(char *progname, int x, int y, int height, int width) {
	size_t offset;
	int r;

	Application *app = calloc(1, sizeof(Application));
	Window *w = &app->window;
	init_window(w, x, y, height, width);

	// add it to the list
	app->next = applications_list;
	applications_list = app;

	// spawn the program
	const char *argv[] = {progname, NULL};
	app->pid = spawn_not_runnable(progname, argv);

	if (app->pid < 0)
		panic("spawn_program: '%s' - %e", progname, app->pid);

	// share the raw canvas memory with the child
	for (offset = 0; offset < w->canvas->size; offset += PGSIZE) {
		void *addr = ((void *) w->canvas->raw_pixels) + offset;
		if ((r = sys_page_map(0, addr, app->pid, CANVAS_BASE + offset, PTE_U | PTE_P | PTE_W)))
			panic("spawn_program sys_page_map: %e", r);
	}

	// start the program
	if (sys_env_set_status(app->pid, ENV_RUNNABLE) < 0)
		panic("spawn_program sys_env_set_status");

	// send the canvas struct to the child
	share(app->pid, w->canvas, sizeof(Canvas));
}

// this code looks complicated because it's hand-optimized since it's
// performance-critical. It should run fast even on -O0.
static void draw_window(Window *w) {
	int x, y;
	int wx = w->x_pos, wy = w->y_pos;

	Canvas *c = w->canvas;
	Pixel *pixels = c->raw_pixels;

	size_t c_height = c->height;
	size_t c_width = c->width;


	// for now, just draw the canvas, no border/buttons/etc

	int tmp = ((width+pitch)>>2);
	for (y = 0; y < c_height; y++) {
		// these ugly variables are here since we manually lift computations
		// outside of the inner loop when possible
		int tmp2 = (y + wy)*tmp;
		int tmp3 = y*c_width;
		for (x = 0; x < c_width; x++) {
			int index = tmp2 + x + wx;
			Pixel p = pixels[tmp3 + x];
			
			// inline do_draw_pixel for speed
			if (zbuffer[index] != p)
				zbuffer[index] = p;
		}
	}
}

static void draw_applications() {
	Application *app;
	for (app = applications_list; app; app = app->next) {
		draw_window(&app->window);
	}
}

static void transmit_events() {
	struct graphics_event *ev = events_queue, *prev = NULL, *next;
	int r;

	while (ev) {
		next = ev->next;
		
		// try to transmit the event
		r = try_share(ev->recipient, ev, sizeof(struct graphics_event));

		if (!r) {
			// we sent the event, so remove it from the queue
			goto remove_event;
		}
		else if (r == -E_IPC_NOT_RECV) {
			// that's okay, try again later
		}
		else if (r == -E_BAD_ENV) {
			cprintf("giving up on event because 0x%x is dead\n", ev->recipient);
		}
		else {
			cprintf("unexpected error in transmit_events: %e\n", r);
		}
		
		goto cont;

remove_event:
		if (ev == events_queue)
			events_queue = next;
		else if (prev) {
			prev->next = next;
		} 
		free(ev);
		ev = NULL;

cont:
		prev = ev;
		ev = next;
	}
}


void umain(int argc, char **argv) {

	cprintf("graphics environment started!\n");
	binaryname = "displayserver";

	init_lfb();
	init_zbuffer();

	spawn_program("paint", 10, 10, 300, 500);

	while (1) {

		transmit_events();

		if (!process_events())
			continue;

		draw_background();
		draw_applications();
		draw_cursor();
		refresh_screen();
	}

}
