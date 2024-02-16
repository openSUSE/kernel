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
