

.section .text
.globl test
.type test, @function

test:

.intel_syntax noprefix
	mov eax, cr0
	and eax, 0xfffffffe # clear PE bit
	mov cr0, eax

.code16

	mov eax, 0x4f03
	int 0x10

.code32

	ret

.att_syntax prefix
