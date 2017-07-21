[BITS 64]

SECTION .text

global _context_switch
global finit, fxsave, fxrstor
global ts_set, ts_clear

%macro context_save 0
	push	rbp
	push	rax
	push	rbx
	push	rcx
	push	rdx
	push	rdi
	push	rsi
	push	r8
	push	r9
	push	r10
	push	r11
	push	r12
	push	r13
	push	r14
	push	r15
	
	mov	ax, ds
	push	rax
	mov	ax, es
	push	rax
	push	fs
	push	gs
	
	mov	ax, 0x10
	mov	ds, ax
	mov	es, ax
	mov	gs, ax
	mov	fs, ax
%endmacro

%macro context_load 0
	pop	gs
	pop	fs
	pop	rax
	mov	es, ax
	pop	rax
	mov	ds, ax
	
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	r11
	pop	r10
	pop	r9
	pop	r8
	pop	rsi
	pop	rdi
	pop	rdx
	pop	rcx
	pop	rbx
	pop	rax
	pop	rbp
%endmacro

_context_switch:
	push	rbp
	mov	rbp, rsp
	
	; Check arg[0] is null
	pushfq
	cmp	rdi, 0
	je	.load
	popfq
	
	; Save context
	push	rax
	
	mov	ax, ss
	mov	qword[rdi + 23 * 8], rax
	
	mov	rax, rbp
	add	rax, 16
	mov	qword[rdi + 22 * 8], rax
	
	pushfq
	pop	rax
	mov	qword[rdi + 21 * 8], rax
	
	mov	ax, cs
	mov	qword[rdi + 20 * 8], rax
	
	mov	rax, qword[rbp + 8]
	mov	qword[rdi + 19 * 8], rax
	
	pop	rax
	pop	rbp
	
	add	rdi, 19 * 8
	mov	rsp, rdi
	sub	rdi, 19 * 8
	
	context_save
	
.load:	; Load conext
	mov	rsp, rsi
	
	context_load
	
	; refreshing cr3 is already done by loader
	mov	rax, cr3
	mov	cr3, rax
	sti
	
	iretq

finit:
	finit
	ret

fxsave:
	fxsave	[rdi]
	ret

fxrstor:
	fxrstor	[rdi]
	ret

ts_set:
	push	rax
	
	mov	rax, cr0
	or	rax, 0x08
	mov	cr0, rax
	
	pop	rax
	ret

ts_clear:
	clts
	ret
