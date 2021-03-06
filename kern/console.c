/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/memlayout.h>
#include <inc/kbdreg.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/graphics.h>
#include <kern/console.h>
#include <kern/trap.h>
#include <kern/picirq.h>

#define CONSBUFSIZE 512

static struct {
	uint8_t buf[CONSBUFSIZE];
	uint32_t rpos;
	uint32_t wpos;
} cons;


static void cons_putc(int c);

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
	inb(0x84);
	inb(0x84);
	inb(0x84);
	inb(0x84);
}

/***** Serial I/O code *****/

#define COM1		0x3F8

#define COM_RX		0	// In:	Receive buffer (DLAB=0)
#define COM_TX		0	// Out: Transmit buffer (DLAB=0)
#define COM_DLL		0	// Out: Divisor Latch Low (DLAB=1)
#define COM_DLM		1	// Out: Divisor Latch High (DLAB=1)
#define COM_IER		1	// Out: Interrupt Enable Register
#define   COM_IER_RDI	0x01	//   Enable receiver data interrupt
#define COM_IIR		2	// In:	Interrupt ID Register
#define COM_FCR		2	// Out: FIFO Control Register
#define COM_LCR		3	// Out: Line Control Register
#define	  COM_LCR_DLAB	0x80	//   Divisor latch access bit
#define	  COM_LCR_WLEN8	0x03	//   Wordlength: 8 bits
#define COM_MCR		4	// Out: Modem Control Register
#define	  COM_MCR_RTS	0x02	// RTS complement
#define	  COM_MCR_DTR	0x01	// DTR complement
#define	  COM_MCR_OUT2	0x08	// Out2 complement
#define COM_LSR		5	// In:	Line Status Register
#define   COM_LSR_DATA	0x01	//   Data available
#define   COM_LSR_TXRDY	0x20	//   Transmit buffer avail
#define   COM_LSR_TSRE	0x40	//   Transmitter off

static bool serial_exists;

static int
serial_proc_data(void)
{
	if (!(inb(COM1+COM_LSR) & COM_LSR_DATA))
		return -1;
	return inb(COM1+COM_RX);
}

void drain_serial(void) {
	if (!serial_exists)
		return;

	int c;
	while ((c = serial_proc_data()) != -1) {
		if (c == 0)
			continue;
		cons.buf[cons.wpos++] = c;
		if (cons.wpos == CONSBUFSIZE)
			cons.wpos = 0;
	}
}

static void
serial_putc(int c)
{
	int i;

	for (i = 0;
	     !(inb(COM1 + COM_LSR) & COM_LSR_TXRDY) && i < 12800;
	     i++)
		delay();

	outb(COM1 + COM_TX, c);
}

static void
serial_init(void)
{
	// Turn off the FIFO
	outb(COM1+COM_FCR, 0);

	// Set speed; requires DLAB latch
	outb(COM1+COM_LCR, COM_LCR_DLAB);
	outb(COM1+COM_DLL, (uint8_t) (115200 / 9600));
	outb(COM1+COM_DLM, 0);

	// 8 data bits, 1 stop bit, parity off; turn off DLAB latch
	outb(COM1+COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);

	// No modem controls
	outb(COM1+COM_MCR, 0);
	// Enable rcv interrupts
	outb(COM1+COM_IER, COM_IER_RDI);

	// Clear any preexisting overrun indications and interrupts
	// Serial port doesn't exist if COM_LSR returns 0xFF
	serial_exists = (inb(COM1+COM_LSR) != 0xFF);
	(void) inb(COM1+COM_IIR);
	(void) inb(COM1+COM_RX);

	// Enable serial interrupts
	if (serial_exists)
		irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_SERIAL));
}



/***** Parallel port output code *****/
// For information on PC parallel port programming, see the class References
// page.

static void
lpt_putc(int c)
{
	int i;

	for (i = 0; !(inb(0x378+1) & 0x80) && i < 12800; i++)
		delay();
	outb(0x378+0, c);
	outb(0x378+2, 0x08|0x04|0x01);
	outb(0x378+2, 0x08);
}




/***** Text-mode CGA/VGA display output *****/

static unsigned addr_6845;
static uint16_t *crt_buf;
static uint16_t crt_pos;

static void
cga_init(void)
{
	volatile uint16_t *cp;
	uint16_t was;
	unsigned pos;

	cp = (uint16_t*) (KERNBASE + CGA_BUF);
	was = *cp;
	*cp = (uint16_t) 0xA55A;
	if (*cp != 0xA55A) {
		cp = (uint16_t*) (KERNBASE + MONO_BUF);
		addr_6845 = MONO_BASE;
	} else {
		*cp = was;
		addr_6845 = CGA_BASE;
	}

	/* Extract cursor location */
	outb(addr_6845, 14);
	pos = inb(addr_6845 + 1) << 8;
	outb(addr_6845, 15);
	pos |= inb(addr_6845 + 1);

	crt_buf = (uint16_t*) cp;
	crt_pos = pos;
}



static void
cga_putc(int c)
{
	// if no attribute given, then use black on white
	if (!(c & ~0xFF))
		c |= 0x0700;

	switch (c & 0xff) {
	case '\b':
		if (crt_pos > 0) {
			crt_pos--;
			crt_buf[crt_pos] = (c & ~0xff) | ' ';
		}
		break;
	case '\n':
		crt_pos += CRT_COLS;
		/* fallthru */
	case '\r':
		crt_pos -= (crt_pos % CRT_COLS);
		break;
	case '\t':
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		cons_putc(' ');
		break;
	default:
		crt_buf[crt_pos++] = c;		/* write the character */
		break;
	}

	// What is the purpose of this?
	if (crt_pos >= CRT_SIZE) {
		int i;

		memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
		for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
			crt_buf[i] = 0x0700 | ' ';
		crt_pos -= CRT_COLS;
	}

	/* move that little blinky thing */
	outb(addr_6845, 14);
	outb(addr_6845 + 1, crt_pos >> 8);
	outb(addr_6845, 15);
	outb(addr_6845 + 1, crt_pos);
}


/***** Keyboard input code *****/

#define NO		0

#define SHIFT		(1<<0)
#define CTL		(1<<1)
#define ALT		(1<<2)

#define CAPSLOCK	(1<<3)
#define NUMLOCK		(1<<4)
#define SCROLLLOCK	(1<<5)

#define E0ESC		(1<<6)

static uint8_t shiftcode[256] =
{
	[0x1D] = CTL,
	[0x2A] = SHIFT,
	[0x36] = SHIFT,
	[0x38] = ALT,
	[0x9D] = CTL,
	[0xB8] = ALT
};

static uint8_t togglecode[256] =
{
	[0x3A] = CAPSLOCK,
	[0x45] = NUMLOCK,
	[0x46] = SCROLLLOCK
};

static uint8_t normalmap[256] =
{
	NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',	// 0x00
	'7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',	// 0x10
	'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',	// 0x20
	'\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
	'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] = KEY_HOME,	      [0x9C] = '\n' /*KP_Enter*/,
	[0xB5] = '/' /*KP_Div*/,      [0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,	      [0xCB] = KEY_LF,
	[0xCD] = KEY_RT,	      [0xCF] = KEY_END,
	[0xD0] = KEY_DN,	      [0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,	      [0xD3] = KEY_DEL
};

static uint8_t shiftmap[256] =
{
	NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',	// 0x00
	'&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',	// 0x10
	'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',	// 0x20
	'"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
	'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] = KEY_HOME,	      [0x9C] = '\n' /*KP_Enter*/,
	[0xB5] = '/' /*KP_Div*/,      [0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,	      [0xCB] = KEY_LF,
	[0xCD] = KEY_RT,	      [0xCF] = KEY_END,
	[0xD0] = KEY_DN,	      [0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,	      [0xD3] = KEY_DEL
};

#define C(x) (x - '@')

static uint8_t ctlmap[256] =
{
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
	C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
	C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
	C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO,
	NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
	C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
	[0x97] = KEY_HOME,
	[0xB5] = C('/'),		[0xC8] = KEY_UP,
	[0xC9] = KEY_PGUP,		[0xCB] = KEY_LF,
	[0xCD] = KEY_RT,		[0xCF] = KEY_END,
	[0xD0] = KEY_DN,		[0xD1] = KEY_PGDN,
	[0xD2] = KEY_INS,		[0xD3] = KEY_DEL
};

static uint8_t *charcode[4] = {
	normalmap,
	shiftmap,
	ctlmap,
	ctlmap
};

static int get_data_from_keyboard() {
	int c;
	uint8_t data;
	static uint32_t shift;

	data = inb(KBDATAP);

	if (data == 0xE0) {
		// E0 escape character
		shift |= E0ESC;
		return 0;
	} else if (data & 0x80) {
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if (shift & E0ESC) {
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];

	c = charcode[shift & (CTL | SHIFT)][data];
	if (shift & CAPSLOCK) {
		if ('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if ('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	// Process special keys
	// Ctrl-Alt-Del: reboot
	if (!(~shift & (CTL | ALT)) && c == KEY_DEL) {
		cprintf("Rebooting!\n");
		outb(0x92, 0x3); // courtesy of Chris Frost
	}

	return c;
}

static void wait_before_reading_from_mouse();
static int get_mouse_status(struct cursor *out) {
	int i;
	int32_t status, delta_x, delta_y;

	wait_before_reading_from_mouse();
	status = inb(KBDATAP);

	wait_before_reading_from_mouse();
	delta_x = inb(KBDATAP);

	wait_before_reading_from_mouse();
	delta_y = inb(KBDATAP);

	if (!(status & MOUSE_ALWAYS_1)) {
		cprintf("warning: mouse packet misalignment\n");
		return 0;
	}

	// ignore overflowing packets
	if ((status & MOUSE_X_OVERFLOW) || (status & MOUSE_Y_OVERFLOW))
		return 0;
	
	if (status & MOUSE_X_SIGNED)
		delta_x |= 0xffffff00;

	if (status & MOUSE_Y_SIGNED)
		delta_y |= 0xffffff00;
	
	out->x += delta_x;

	// the y-direction is reported by the mouse such that up is negative;
	// reverse it here for sanity
	out->y -= delta_y;

	// make sure we cannot go out of bounds
	out->x = MAX(out->x, 0);
	out->x = MIN(out->x, GRAPHICS_WIDTH - 1);
	out->y = MAX(out->y, 0);
	out->y = MIN(out->y, GRAPHICS_HEIGHT - 1);

	out->left_pressed = (status & MOUSE_LEFT_BTN);
	out->right_pressed = (status & MOUSE_RIGHT_BTN);
	out->middle_pressed = (status & MOUSE_MID_BTN);

	return 1;
}

static bool cursor_eq(struct cursor *a, struct cursor *b) {
	return (
		a->x == b->x && 
		a->y == b->y &&
		a->left_pressed == b->left_pressed && 
		a->right_pressed == b->right_pressed && 
		a->middle_pressed == b->middle_pressed
	);
}

void handle_mouse_event() {
	struct cursor new_cursor = cursor;

	if (!get_mouse_status(&new_cursor))
		return;
	
	// if any keys were pressed, we first need to generate an event with the
	// current mouse position, so the click goes at the right place.
	if ((new_cursor.left_pressed && !cursor.left_pressed) ||
		(new_cursor.middle_pressed && !cursor.middle_pressed) ||
		(new_cursor.right_pressed && !cursor.right_pressed)) {
		struct io_event e = {MOUSE_MOVE, {new_cursor.x, new_cursor.y}};
		io_event_put(&e);
	}
	
	if (new_cursor.left_pressed && !cursor.left_pressed) {
		struct io_event e = {MOUSE_CLICK, {0, 0}};
		io_event_put(&e);
	}

	if (new_cursor.middle_pressed && !cursor.middle_pressed) {
		struct io_event e = {MOUSE_CLICK, {1, 0}};
		io_event_put(&e);
	}

	if (new_cursor.right_pressed && !cursor.right_pressed) {
		struct io_event e = {MOUSE_CLICK, {2, 0}};
		io_event_put(&e);
	}

	cursor = new_cursor;
}

static void add_to_console_buffer(char c) {
	cons.buf[cons.wpos++] = c;
	if (cons.wpos == CONSBUFSIZE)
		cons.wpos = 0;
}

static void add_to_io_event_queue(char c) {
	struct io_event e = {KEYBOARD_KEY, {c, 0}};
	io_event_put(&e);
}

void handle_keyboard_event() {
	int c;
	if ((c = get_data_from_keyboard())) {
		if (have_graphics)
			add_to_io_event_queue(c);
		else
			add_to_console_buffer(c);
	}
}

/* we have to drain keyboard and mouse at the same time, because they both go
 * via the same port (they're both PS/2 devices). */
void drain_keyboard_and_mouse() {
	uint8_t stat;
	struct cursor old_cursor = cursor;

	while ((stat = inb(KBSTATP)) & KBS_DIB) {
		if (stat & KBS_FROM_MOUSE)
			handle_mouse_event();
		else
			handle_keyboard_event();
	}

	// generate a mouse io event if x/y changed
	if (cursor.x != old_cursor.x || cursor.y != old_cursor.y) {
		struct io_event e = {MOUSE_MOVE, {cursor.x, cursor.y}};
		io_event_put(&e);
	}
}

// initializes the PS/2 keyboard
static void
kbd_init(void)
{
	// Drain the kbd buffer so that QEMU generates interrupts.
	drain_keyboard_and_mouse();
	irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_KBD));
}

/***** General device-independent console code *****/
// Here we manage the console input buffer,
// where we stash characters received from the keyboard or serial port
// whenever the corresponding interrupt occurs.


// return the next input character from the console, or 0 if none waiting
int
cons_getc(void)
{
	int c;

	// poll for any pending input characters,
	// so that this function works even when interrupts are disabled
	// (e.g., when called from the kernel monitor).
	drain_serial();
	drain_keyboard_and_mouse();

	// grab the next character from the input buffer.
	if (cons.rpos != cons.wpos) {
		c = cons.buf[cons.rpos++];
		if (cons.rpos == CONSBUFSIZE)
			cons.rpos = 0;
		return c;
	}
	return 0;
}

// output a character to the console
static void
cons_putc(int c)
{
	serial_putc(c);
	lpt_putc(c);
	cga_putc(c);
}

// waits until the mouse is ready to handle more input
static void wait_before_sending_to_mouse() {
	uint8_t stat;
	do {
		stat = inb(KBSTATP);
	} while (stat & KBS_IBF);
}

// waits until the mouse has sent us some data over serial
static void wait_before_reading_from_mouse() {
	uint8_t stat;
	do {
		stat = inb(KBSTATP);
	} while (!(stat & KBS_DIB));
}

static void wait_for_ack() {
	wait_before_reading_from_mouse();
	uint8_t stat;
	do {
		stat = inb(KBOUTP);
	} while (!(stat & KBR_ACK));
}

static void mouse_send_command(uint8_t command) {
	// address the second device
	wait_before_sending_to_mouse();
	outb(KBCMDP, KBC_AUXWRITE); 

	// actually write the byte
	wait_before_sending_to_mouse();
	outb(KBDATAP, command);
}

static void enable_irq12() {
	uint8_t status;

	wait_before_sending_to_mouse();
	outb(KBCMDP, KBC_RAMREAD);

	wait_before_reading_from_mouse();
	status = inb(KBDATAP);
	status |= KBD_WANT_IRQ12;
	assert (!(status & KBD_DISABLE_MOUSE_INTERFACE));

	wait_before_sending_to_mouse();
	outb(KBCMDP, KBC_RAMWRITE);
	wait_before_sending_to_mouse();
	outb(KBDATAP, status);
}

// initializes the PS/2 mouse
static void mouse_init() {
	cursor.x = cursor.y = 0;

	// enable auxiliary device (mouse)
	wait_before_sending_to_mouse();
	outb(KBCMDP, KBC_AUXENABLE);

	// tell the mouse that we want it to generate IRQ 12 when there are new
	// events
	enable_irq12();

	// use default settings
	mouse_send_command(KBC_SETDEFAULT);
	wait_for_ack();

	// allow the mouse to send us packets
	mouse_send_command(KBC_ENABLE);
	wait_for_ack();

	// disable masking of IRQ 12
	drain_keyboard_and_mouse();
	irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_MOUSE));
	drain_keyboard_and_mouse();
}

void init_io() {

	cga_init();
	kbd_init();
	serial_init();

	if (graphics_enabled())
		mouse_init();

	if (!serial_exists)
		cprintf("Serial port does not exist!\n");
}


// `High'-level console I/O.  Used by readline and cprintf.

void
cputchar(int c)
{
	cons_putc(c);
}

int
getchar(void)
{
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
