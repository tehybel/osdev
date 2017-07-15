
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



# the following mode-setting code is inspired by:
# http://wiki.osdev.org/User:Omarrx024/VESA_Tutorial


.set WIDTH, 1024
.set HEIGHT, 768
.set BPP, 32 # bits per pixel



	push es					# some VESA BIOSes destroy ES, or so I read
	mov di, 0xc000
	mov eax, 0x32454256 	# "VBE2"
	mov dword [di], eax
	mov ax, 0x4F00				# get VBE BIOS info
	int 0x10
	pop es

	cmp ax, 0x4F				# BIOS doesn't support VBE?
	jne .error

	mov bx, (0xc000 + 16) # get video_modes (.segment)
	mov fs, [bx]
	mov bx, (0xc000 + 14) # get video_modes (.offset)
	mov si, [bx]

.find_mode:
	mov dx, fs:[si] # mode
	add si, 2

	cmp dx, 0xFFFF			# end of list?
	je .error

	push es
	mov ax, 0x4F01				# get VBE mode info
	mov cx, dx
	mov di, 0xd000
	int 0x10
	pop es

	cmp ax, 0x4F
	jne .error

	mov bx, (0xd000 + 18)
	mov bx, [bx]
	cmp bx, WIDTH
	jne .next_mode

	mov bx, (0xd000 + 20)
	mov bx, [bx]
	cmp bx, HEIGHT
	jne .next_mode

	mov bx, (0xd000 + 25)
	mov bl, [bx]
	cmp bl, BPP
	jne .next_mode

	# If we make it here, we've found the correct mode!

	# Set the mode
	push es
	mov ax, 0x4F02
	mov bx, fs:[si] # mode
	or bx, 0x4000			# enable LFB
	mov di, 0			# not sure if some BIOSes need this... anyway it doesn't hurt
	int 0x10
	pop es

	cmp ax, 0x4F
	jne .error

	clc
	jmp success

.next_mode:
	jmp .find_mode

.error:
	jmp .error

success:



# we've set the video mode!
# now the physical address of the LFB should be in (0xd000 + 40)

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

