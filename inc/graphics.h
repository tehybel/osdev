#ifndef JOS_INC_LIBGRAPHICS_H
#define JOS_INC_LIBGRAPHICS_H 1


#define CANVAS_BASE ((void *) 0x30001000)
#define SHARE_PAGE (CANVAS_BASE - PGSIZE*2)


#define RED(color)   (((color)>>16) & 0xff)
#define GREEN(color) (((color)>> 8) & 0xff)
#define BLUE(color)  (((color)>> 0) & 0xff)

#define COLOR_WHITE 	color (0xff, 0xff, 0xff)
#define COLOR_BLACK 	color (0, 0, 0)
#define COLOR_CYAN 		color (0,255,255)
#define COLOR_YELLOW	color (255,255,0)
#define COLOR_CRIMSON	color (220,20,60)
#define COLOR_LIME		color (50,205,50)




// types and structs

typedef uint32_t Pixel;

typedef struct canvas {
	size_t size;
	int x_pos, y_pos;
	size_t width, height;
	Pixel *raw_pixels;
} Canvas;

Canvas canvas;

enum event_types {
	EVENT_MOUSE_CLICK = 0,
	EVENT_KEY_PRESS,

	// below are custom types which aren't used directly by the display
	// server
	EVENT_RAW_DATA,
};

struct event_mouse_click {
	int x, y;
};

struct event_key_press {
	unsigned char ch;
};

struct event_raw_data {
	size_t size;
	uint8_t data[0];
};

struct graphics_event {
	enum event_types type;
	struct graphics_event *next, *prev;
	union {
		struct event_mouse_click emc;
		struct event_key_press ekp;
		struct event_raw_data erd;
	} d;
};

typedef struct font {
	char *name;
	size_t width, height;
	const unsigned char *data;
} Font;



// globals

Font *font_10x18;



// function declarations


void init_graphics();
void draw_pixel(const int x, const int y, const int color);
int color(int r, int g, int b);
void draw_square(int x, int y, int side_length);
void color_canvas(int col);
void event_loop(void (*process_event)(struct graphics_event *));
void draw_text(char *text, int x, int y, int col, Font *font);
void draw_char(unsigned char ch, int begin_x, int begin_y, int col,
					  Font *font, void (*draw)(int, int, int));
void init_fonts();

#endif
