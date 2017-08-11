#ifndef DEF_DISPLAYSERVER
#define DEF_DISPLAYSERVER

#define LFB_BASE 0xff000000
#define ZBUFFER_BASE 0xa0000000

#define NUM_EVENTS 100

#define CURSOR_SIZE 20

#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

#define COLOR_WHITE color(0xff, 0xff, 0xff)

typedef struct canvas {
	size_t size;
	void *raw_pixels;
} Canvas;

typedef struct window {
	int height, width;
	int xpos, ypos;
	struct window *next;
	Canvas *canvas;
} Window;

#endif
