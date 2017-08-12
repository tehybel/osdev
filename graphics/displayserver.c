#include <inc/lib.h>
#include <inc/graphics.h>
#include <kern/graphics.h>
#include <graphics/displayserver.h>


uint32_t *lfb;
uint32_t *zbuffer;

size_t lfb_size;
size_t pitch;
size_t width;
size_t height;
size_t bpp;

int cursor_x, cursor_y;

struct io_event events[NUM_EVENTS];

// used to share values with child processes
#define SHARE_PAGE (CANVAS_BASE - PGSIZE)

Window * windows_list = NULL;

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
			// we inline do_draw_pixel here for speed
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

static void draw_cursor() {
	int x1 = MAX(0, cursor_x);
	int x2 = MIN(cursor_x + CURSOR_SIZE, width);
	int y1 = MAX(0, cursor_y);
	int y2 = MIN(cursor_y + CURSOR_SIZE, height);
	draw_rectangle(x1, y1, x2, y2, COLOR_WHITE);
}

static Window *get_window_for_coordinate(int x, int y) {
	Window *w;
	for (w = windows_list; w; w = w->next) {
		if (x < w->x_pos)
			continue;
		if (y < w->y_pos)
			continue;
		if (x >= w->x_pos + w->width)
			continue;
		if (y >= w->y_pos + w->height)
			continue;

		return w;
	}

	return NULL;
}

static void add_event_to_queue(struct graphics_event *ev) {
	ev->next = events_queue;
	events_queue = ev;
}

static void handle_mouse_click(int button) {
	Window *w = get_window_for_coordinate(cursor_x, cursor_y);
	if (!w) return;

	struct graphics_event *ev = calloc(1, sizeof(struct graphics_event));
	if (!ev) panic("malloc event");

	ev->type = EVENT_MOUSE_CLICK;
	ev->d.emc.x = cursor_x;
	ev->d.emc.y = cursor_y;
	ev->recipient = w->pid;

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

Window * alloc_window() {
	Window *w = malloc(sizeof(Window));
	if (!w) panic("alloc_window");

	w->x_pos = 10;
	w->y_pos = 10;

	w->height = 500;
	w->width = 800;

	w->next = NULL;

	w->canvas = alloc_canvas(w);

	return w;
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

static void spawn_program(char *progname) {
	size_t offset;
	int r;

	Window *w = alloc_window();

	// add it to the list
	w->next = windows_list;
	windows_list = w;


	// spawn the program
	const char *argv[] = {progname, NULL};
	w->pid = spawn_not_runnable(progname, argv);

	if (w->pid < 0)
		panic("spawn_program: '%s' - %e", progname, w->pid);

	// share the raw canvas memory with the child
	for (offset = 0; offset < w->canvas->size; offset += PGSIZE) {
		void *addr = ((void *) w->canvas->raw_pixels) + offset;
		if ((r = sys_page_map(0, addr, w->pid, CANVAS_BASE + offset, PTE_U | PTE_P | PTE_W)))
			panic("spawn_program sys_page_map: %e", r);
	}

	// start the program
	if (sys_env_set_status(w->pid, ENV_RUNNABLE) < 0)
		panic("spawn_program sys_env_set_status");

	// send the canvas struct to the child
	share(w->pid, w->canvas, sizeof(Canvas));
}

static void draw_window(Window *w) {
	int x, y;

	Canvas *c = w->canvas;

	// for now, just draw the canvas, no border/buttons/etc
	for (x = 0; x < c->width; x++) {
		for (y = 0; y < c->height; y++) {
			Pixel p = c->raw_pixels[y*c->width + x];
			do_draw_pixel(w->x_pos + x, w->y_pos + y, p);
		}
	}

}

static void draw_windows() {
	Window *w;
	for (w = windows_list; w; w = w->next) {
		draw_window(w);
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

	spawn_program("testcanvas");

	while (1) {

		transmit_events();

		if (!process_events())
			continue;

		draw_background(); // slow
		draw_windows();
		draw_cursor();
		refresh_screen(); // slow
	}

}
