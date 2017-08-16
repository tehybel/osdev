#ifndef DEF_DISPLAYSERVER
#define DEF_DISPLAYSERVER

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

#define NUM_EVENTS 100

#define CURSOR_SIZE 16

typedef struct window {
	char *title;
	int height, width;
	int x_pos, y_pos;
	Canvas *canvas;
} Window;

typedef struct application {
	Window window;
	int pid;
	struct application *next;
} Application;

struct event_list_head {
	int pid;
	struct graphics_event *link;
	struct event_list_head *next;
};

#endif
