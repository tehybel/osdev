#include <inc/lib.h>
#include <inc/graphics.h>

// how much to indent text in both the x- and y-direction
#define BORDER_SIZE 10

int cursor_x, cursor_y;
Font *font;

// this holds the current input
#define INBUF_SIZE 1024
unsigned char inbuf[INBUF_SIZE];
size_t inbuf_index = 0;


// the output buffer is a circular list of lines
struct outbuf {
	char **lines; // the raw data
	size_t line_index; // current index into the circular list
	size_t char_index;
	size_t num_lines; // number of lines in the circular list
	size_t line_size; // number of chars per line
} outbuf;


static void draw() {
	// draw the canvas background
	color_canvas(COLOR_BLACK);
	
	// draw each line in the output buffer
	size_t i;
	for (i = 0; i < outbuf.num_lines; i++) {
		int index = (i + outbuf.line_index + 1) % outbuf.num_lines;
		int y = i*font->height + BORDER_SIZE;
		draw_text(outbuf.lines[index], BORDER_SIZE, y, COLOR_WHITE, font);
	}
}

static void clear_line(char *line) {
	memset(line, ' ', outbuf.line_size);
	line[outbuf.line_size] = '\0';
}

static void drain_inbuf() {
	// TODO
}

static void add_to_outbuf(unsigned char ch) {
	if (ch == '\n') {
		// skip to a new line
		outbuf.char_index = 0;
	}
	else {
		outbuf.lines[outbuf.line_index][outbuf.char_index] = ch;
		outbuf.char_index = (outbuf.char_index + 1) % outbuf.line_size;
	}

	if (outbuf.char_index == 0) {
		// we're now on a new line; clear it 's it's ready to use, and advance
		// the line_index.
		outbuf.line_index = (outbuf.line_index + 1) % outbuf.num_lines;
		clear_line(outbuf.lines[outbuf.line_index]);
	}
}

static void add_to_inbuf(unsigned char ch) {
	if (inbuf_index >= INBUF_SIZE)
		panic("inbuf full");
	
	inbuf[inbuf_index++] = ch;
}

static void handle_inchar(unsigned char ch) {
	add_to_outbuf(ch);
	if (ch != '\n')
		add_to_inbuf(ch);
	else
		drain_inbuf();
	
	// since something changed, we should redraw everything
	draw();
}

static void process_event(struct graphics_event *ev) {

	// ignore mouse events for now.
	if (ev->type != EVENT_KEY_PRESS)
		return;
	
	handle_inchar(ev->d.ekp.ch);
}

static void init_outbuf() {
	int i;

	outbuf.line_size = (canvas.width - 2*BORDER_SIZE) / font->width;
	outbuf.num_lines = (canvas.height - 2*BORDER_SIZE) / font->height;
	outbuf.line_index = outbuf.char_index = 0;
	outbuf.lines = malloc(outbuf.num_lines * sizeof(char *));
	if (!outbuf.lines)
		panic("alloc lines");
	

	for (i = 0; i < outbuf.num_lines; i++) {
		outbuf.lines[i] = malloc(outbuf.line_size + 1);
		if (!outbuf.lines[i])
			panic("alloc lines[i]");
		clear_line(outbuf.lines[i]);
	}
}

void umain(int argc, char **argv) {
	init_graphics();

	// set the font
	font = font_10x18;

	// init the cursor
	cursor_x = BORDER_SIZE;
	cursor_y = BORDER_SIZE;

	// init the output buffer
	init_outbuf();

	// draw everything
	draw();

	// wait for keyboard events
	event_loop(process_event);
}
