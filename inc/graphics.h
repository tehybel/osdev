#ifndef JOS_INC_LIBGRAPHICS_H
#define JOS_INC_LIBGRAPHICS_H 1

#define CANVAS_BASE ((void *) 0x30000000)

void init_graphics();

typedef struct canvas {
	size_t size;
	void *raw_pixels;
} Canvas;

#endif
