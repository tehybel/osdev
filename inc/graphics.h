#ifndef JOS_INC_GRAPHICS_H
#define JOS_INC_GRAPHICS_H


#define GRAPHICS_HEIGHT 768
#define GRAPHICS_WIDTH 1024
// bits per pixel
#define GRAPHICS_BPP 32


// VA at which the LFB resides
#define LFB_BASE 0xff000000


#ifndef __ASSEMBLER__

struct graphics {
	physaddr_t lfb_pa;
	size_t lfb_size; // number of bytes in the LFB
	void *lfb; // VA of the LFB
} graphics;

int do_init_graphics();
extern void *realmode_gdt;


#endif /* __ASSEMBLER */
#endif /* !JOS_INC_GRAPHICS_H */
