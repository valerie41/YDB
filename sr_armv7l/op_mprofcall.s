/* op_mprofcall.s */

	.title	op_mprofcall.s

.include "linkage.si"
.include "g_msf.si"
.include "debug.si"

	.sbttl	op_mprofcallb

	.data
.extern	frame_pointer

	.text
.extern	copy_stack_frame_sp
	

ENTRY op_mprofcallb
ENTRY op_mprofcallw
ENTRY op_mprofcalll
	push	{r4, lr}			/* r4 is to maintain 8 byte stack alignment */
	CHKSTKALIGN				/* Verify stack alignment */
	ldr	r12, [r5]
	add	r0, lr, #4			/* Bump return pc past the branch instruction following bl that got us here */
	str	r0, [r12, #msf_mpc_off]		/* and store it in Mumps stack frame */
	bl	copy_stack_frame_sp
	pop	{r4, pc}

.end
