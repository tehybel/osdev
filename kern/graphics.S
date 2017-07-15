
.intel_syntax noprefix

.section .text

.globl do_init_graphics
.type do_init_graphics, @function


do_init_graphics:

# first we need to switch to real mode to use int 0x10.

#     Disable the interrupts:
#         Turn off maskable interrupts using CLI.
#         Disable NMI (optional).
	cli

#     Turn off paging:
#         Transfer control to a 1:1 page.
# - we already did this from C code

#         Ensure that the GDT and IDT are in a 1:1 page.
# - this should already be the case; we're using a bootstrap GDT, and the IDT
#   isn't even loaded

#         Clear the PG-flag in the zeroth control register.
	mov eax, cr0
	and eax, ~0x80000000
	mov cr0, eax

#         Set the third control register to 0.
	xor eax, eax
	mov cr3, eax

#     Use GDT with 16-bit tables (skip this step if one is already available):
#         Create a new GDT with a 16-bit data and code segment:
#             Limit: 0xFFFFF
#             Base: 0x0
#             16-bit
#             Privilege level: 0
#             Granularity: 0
#             Read and Write: 1
#         Load new GDT ensuring that the currently used selectors will remain the same (index in cs/ds/ss will be copy of original segment in new GDT)
	mov eax, 0x9000 # the gdt descriptor must be there
	lgdt [eax]

#     Far jump to 16-bit protected mode:
#         Far jump to 16-bit protected mode with a 16-bit segment index.
	ljmp 0x8:(0x8000 + 1f - do_init_graphics)

1:

.code16
	
#     Load data segment selectors with 16-bit indexes:
#         Load ds, es, fs, gs, ss with a 16-bit data segment.
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

#     Load real mode IDT:
#         Limit: 0x3FF
#         Base 0x0
#         Use lidt
	mov ax, (0x9000 + realmode_idt - realmode_gdt)
	lidt [eax]

#     Disable protected mode:
#         Set PE bit in CR0 to false.
	mov eax, cr0
	and eax, 0x7FFFFFFe
	mov cr0, eax

#     Far jump to real mode:
#         Far jump to real mode with real mode segment selector (usually 0).
	ljmp 0x0:(0x8000 + 1f - do_init_graphics)

1:

#     Reload data segment registers with real mode values:
#         Load ds, es, fs, gs, ss with appropriate real mode values (usually 0).
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

#     Set stack pointer to appropriate value:
#         Set sp to stack value that will not interfere with real mode program.
	mov sp, 0xa000

#     Enable interrupts:
#         Enable maskable interrupts with STI.
	sti

#     Continue on in real mode with all bios interrupts.



# now we can use int 0x10
# thus we will set the video mode

	mov ax, 0x4f03
	int 0x10






# finally we must switch back to 32 bits protected mode
# TODO

	ret


.globl realmode_gdt

realmode_gdt:

# table descriptor
.word   (3 * 8) - 1                 # GDT table limit
.long   0x9008            			# GDT table location
.word 0 # padding

# null descriptor
.long   0x0
.long   0x0
                
# cs descriptor
.word   0xFFFF                      # Limit 15:0
.word   0x0                         # Base 15:0
.byte   0x0                         # Base 23:16
.byte   0x9A                        # Access: Present, Ring 0, Code Segment
.byte   0x8F                        # Granularity: 1Kb, 16-bit mode
.byte   0x00                        # Base 31:24

# ds descriptor
.word   0xFFFF                      # Limit 15:0
.word   0x0                         # Base 15:0
.byte   0x0                         # Base 23:16
.byte   0x92                        # Access: Present, Ring 0, Data Segment
.byte   0x8F                        # Granularity: 1kB, 16-bit mode
.byte   0x00                        # Base 31:24


realmode_idt:
.word   0x3FF                       # IDT Table Limit
.long   0x0                         # IDT Table base



.att_syntax prefix

