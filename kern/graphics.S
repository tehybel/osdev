

.intel_syntax noprefix

.section .text

.globl do_init_graphics
.type do_init_graphics, @function


do_init_graphics:
	
	call switch_to_real_mode
	call set_video_mode
	call switch_to_protected_mode

	ret


# for details, see http://wiki.osdev.org/Real_Mode
switch_to_real_mode:
# The program needs to go through the following steps:
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
# - for now, let's try to ignore this and see what happens..
# see boot/lib/pcat/x64/realmexe.S in minoca for help

#     Far jump to 16-bit protected mode:
#         Far jump to 16-bit protected mode with a 16-bit segment index.
#     Load data segment selectors with 16-bit indexes:
#         Load ds, es, fs, gs, ss with a 16-bit data segment.
#     Load real mode IDT:
#         Limit: 0x3FF
#         Base 0x0
#         Use lidt
#     Disable protected mode:
#         Set PE bit in CR0 to false.
#     Far jump to real mode:
#         Far jump to real mode with real mode segment selector (usually 0).
#     Reload data segment registers with real mode values:
#         Load ds, es, fs, gs, ss with appropriate real mode values (usually 0).
#     Set stack pointer to appropriate value:
#         Set sp to stack value that will not interfere with real mode program.
#     Enable interrupts:
#         Enable maskable interrupts with STI.
#     Continue on in real mode with all bios interrupts.
	ret

set_video_mode:
	ret

switch_to_protected_mode:
	ret


.att_syntax prefix

