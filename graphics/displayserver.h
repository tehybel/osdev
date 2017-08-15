#ifndef DEF_DISPLAYSERVER
#define DEF_DISPLAYSERVER

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

#define NUM_EVENTS 100

#define CURSOR_SIZE 10

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

#endif
