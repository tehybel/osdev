#ifndef DEF_DISPLAYSERVER
#define DEF_DISPLAYSERVER

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

#define NUM_EVENTS 100

#define CURSOR_SIZE 20

typedef struct window {
	int height, width;
	int xpos, ypos;
	struct window *next;
	Canvas *canvas;
} Window;

#endif
