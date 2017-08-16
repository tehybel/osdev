#include <inc/lib.h>
#include <inc/graphics.h>

// how much to indent text in both the x- and y-direction
#define BORDER_SIZE 10

Font *font;

// this holds the current input
#define INBUF_SIZE 1024
unsigned char inbuf[INBUF_SIZE];
size_t inbuf_index = 0;

int shell_pipe = -1;

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

static void send_to_shell(unsigned char *line) {

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
		// we're now on a new line; clear it so it's ready to use, and advance
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

// take the command that's in the input buffer and process it.
static void drain_inbuf() {
	add_to_inbuf('\0');
	send_to_shell(inbuf);
	inbuf_index = 0;
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

// this is the main() of the runner process -- it will spawn the shell
// process, read output from it, and send it to the terminal emulator via IPC.
static int runner_main() {
	int r;
	int stdout_pipe[2];
	char buf[1024];
	const char *argv[] = {"sh", NULL};

	if (pipe(stdout_pipe))
		panic("runner pipe");
	
	// fix stdout
	if (dup(stdout_pipe[1], 1) < 0)
		panic("runner dup stdout");
	
	if (spawn(argv[0], argv) < 0)
		panic("runner spawn");
	
	cprintf("runner is alive and kicking\n");
	
	while (1) {
		r = read(stdout_pipe[0], buf, sizeof(buf) - 1);
		if (r < 0) {
			cprintf("runner got r=%d\n", r);
		}
		else {
			buf[r] = '\0';
			cprintf("runner got '%s'\n", buf);
			// TODO send it via IPC
		}
	}
}

// spawn the runner process which spawns the shell and hands us its output via
// IPC
static void spawn_runner() {
	int r;
	int _shell_pipe[2];

	if (pipe(_shell_pipe))
		panic("spawn_shell pipe");
	
	r = fork();
	if (r < 0) {
		panic("spawn_shell fork");
	}
	else if (r == 0) {
		/*
		 * since there's no stdin/stdout for this process, calling pipe()
		 * gives the right values, so there's no need to dup()..

		if ((r = dup(_shell_pipe[0], 0)) < 0) // fix stdin
			panic("runner dup stdin: %e", r);
		*/
		assert (_shell_pipe[0] == 0);
		assert (_shell_pipe[1] == 1);

		runner_main();
		exit();
	}

	shell_pipe = _shell_pipe[1];
}

void umain(int argc, char **argv) {
	init_graphics();

	spawn_runner();

	// set the font
	font = font_10x18;

	// init the output buffer
	init_outbuf();

	// draw everything
	draw();

	// wait for keyboard events
	event_loop(process_event);
}
