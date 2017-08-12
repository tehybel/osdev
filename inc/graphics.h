#ifndef JOS_INC_LIBGRAPHICS_H
#define JOS_INC_LIBGRAPHICS_H 1

#define CANVAS_BASE ((void *) 0x30001000)

#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

#define COLOR_WHITE 	color (0xff, 0xff, 0xff)
#define COLOR_CYAN 		color (0,255,255)
#define COLOR_YELLOW	color (255,255,0)
#define COLOR_CRIMSON	color (220,20,60)
#define COLOR_LIME		color (50,205,50)

void init_graphics();
void draw_pixel(const int x, const int y, const int color);
int color(int r, int g, int b);

typedef uint32_t Pixel;

typedef struct canvas {
	size_t size;
	size_t width, height;
	Pixel *raw_pixels;
} Canvas;

Canvas canvas;

enum event_types {
	EVENT_MOUSE_CLICK = 0,
};

struct event_mouse_click {
	int x, y;
};

struct graphics_event {
	enum event_types type;
	union {
		struct event_mouse_click emc;
	} d;
	int recipient;
	struct graphics_event *next;
};

#endif
