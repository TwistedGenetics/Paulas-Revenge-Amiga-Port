# Paula's Revenge 1200 startup and interrupt trampolines (LLVM m68k syntax).
# jsr d16(a6) is written as raw words because the assembler cannot parse it.
# Never use movem with -(sp)/(sp)+ here: LLVM 18 encodes the predecrement mask wrongly.
	.text
	.globl	_start
_start:
	jsr	amiga_entry
	rts

	.globl	run_on_stack
run_on_stack:
	suba.l	#16,%sp
	movem.l	%d2-%d3/%a2/%a6,(%sp)
	move.l	(20,%sp),%a2
	move.l	(24,%sp),%a1
	move.l	(28,%sp),%d2
	move.l	%a1,%d3
	move.l	4,%a6
	move.l	%a2,%a0
	.short	0x4EAE,-732
	move.l	%d2,-(%sp)
	move.l	%d3,%a1
	jsr	(%a1)
	adda.l	#4,%sp
	move.l	%d0,%d2
	move.l	4,%a6
	move.l	%a2,%a0
	.short	0x4EAE,-732
	move.l	%d2,%d0
	movem.l	(%sp),%d2-%d3/%a2/%a6
	adda.l	#16,%sp
	rts

	.globl	vb_tramp
vb_tramp:
	move.l	%a1,-(%sp)
	jsr	vb_handler
	adda.l	#4,%sp
	move.l	#0xdff000,%a0
	move.l	#0,%d0
	rts

	.globl	in_tramp
in_tramp:
	move.l	%a1,-(%sp)
	move.l	%a0,-(%sp)
	jsr	input_handler
	adda.l	#8,%sp
	rts
