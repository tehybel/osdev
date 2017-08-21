#include <inc/lib.h>
#include <inc/graphics.h>

// how much to indent text in both the x- and y-direction
#define BORDER_SIZE 10

Font *font;

// this holds the current input
#define INBUF_SIZE 1024
unsigned char inbuf[INBUF_SIZE];
size_t inbuf_index = 0;

int shell_fd = -1;

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

static void send_to_shell(unsigned char *line, size_t size) {
	int r;
	// cprintf("terminal sends '%s' to shell\n", line);
	r = write(shell_fd, line, size);
	if (r < size) {
		// TODO handle short writes and errors better
		cprintf("send_to_shell: r is only %d\n", r);
	}
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
	send_to_shell(inbuf, inbuf_index);
	inbuf_index = 0;
}

static void handle_inchar(unsigned char ch) {
	add_to_outbuf(ch);
	add_to_inbuf(ch);
	if (ch == '\n')
		drain_inbuf();
	
	// since something changed, we should redraw everything
	draw();
}

// handle raw output coming from a child process
static void handle_raw_data(uint8_t *data, size_t size) {
	size_t i;
	for (i = 0; i < size; i++)
		add_to_outbuf(data[i]);

	draw();
}

static void process_event(struct graphics_event *ev) {
	switch (ev->type) {
	case EVENT_KEY_PRESS:
		handle_inchar(ev->d.ekp.ch);
		break;
	
	case EVENT_MOUSE_CLICK:
		// do nothing for now
		break;
	
	case EVENT_RAW_DATA:
		handle_raw_data(ev->d.erd.data, ev->d.erd.size);
		break;
	
	default:
		cprintf("unhandled ev->type: %d\n", ev->type);
		break;
	}
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
	const char *argv[] = {"sh", NULL};
	envid_t displayserv_pid = thisenv->env_parent_id;

	if (pipe(stdout_pipe))
		panic("runner pipe");
	
	// fix stdout
	if (dup(stdout_pipe[1], 1) < 0)
		panic("runner dup stdout");
	
	if (spawn(argv[0], argv) < 0)
		panic("runner spawn");
	
	while (1) {
		// we have to map the SHARE_PAGE once per loop, because the ipc_send
		// will *share* the page with the display server, and so we should not
		// modify the same physical page again after sending it.
		if (sys_page_alloc(0, SHARE_PAGE, PTE_P | PTE_U | PTE_W))
			panic("runner alloc");

		struct graphics_event *ev = (struct graphics_event *) SHARE_PAGE;

		ev->type = EVENT_RAW_DATA;
		ev->next = ev->prev = NULL;

		r = read(stdout_pipe[0], ev->d.erd.data, 
				 PGSIZE - sizeof(struct graphics_event));

		if (r <= 0) {
			cprintf("runner got r=%d -> %e\n", r, r);
		} else {
			ev->d.erd.size = r;
			ipc_send(displayserv_pid, 0, SHARE_PAGE, PTE_P | PTE_U);
		}

		if (sys_page_unmap(0, SHARE_PAGE))
			panic("runner unmap");
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

	shell_fd = _shell_pipe[1];
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
