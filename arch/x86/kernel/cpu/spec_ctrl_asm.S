#include <asm/segment.h>

	.text
	
	.globl	x86_ibrs_enabled_asm
	.type	x86_ibrs_enabled_asm, @function
x86_ibrs_enabled_asm:
	xorl	%eax, %eax
	cmpl	$1, ibrs_state(%rip)
	sete	%al
	ret
	
	.size	x86_ibrs_enabled_asm, .-x86_ibrs_enabled_asm



	.globl	mds_clear_user
	.type	mds_clear_user, @function
mds_clear_user:
	cmpb	$0, mds_user_clear(%rip)
	je	1f
	verw 	ds(%rip)
1:
	ret
	
	.size	mds_clear_user, .-mds_clear_user
	

	.section	.rodata
	.align 2
	.type	ds.1 @object
	.size	ds.1, 2
ds:
	.value	__KERNEL_DS
	
