/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_VMENTER_H
#define __KVM_X86_VMENTER_H

#define KVM_ENTER_VMRESUME			BIT(0)
#define KVM_ENTER_SAVE_SPEC_CTRL		BIT(1)
#define KVM_ENTER_CLEAR_CPU_BUFFERS_FOR_MMIO	BIT(2)

#ifdef __ASSEMBLER__
.macro RESTORE_GUEST_SPEC_CTRL_BODY guest_spec_ctrl:req, label:req
	/*
	 * SPEC_CTRL handling: if the guest's SPEC_CTRL value differs from the
	 * host's, write the MSR.  This is kept out-of-line so that the common
	 * case does not have to jump.
	 *
	 * IMPORTANT: To avoid RSB underflow attacks and any other nastiness,
	 * there must not be any returns or indirect branches between this code
	 * and vmentry.
	 */
#ifdef CONFIG_X86_64
	mov \guest_spec_ctrl, %rdx
	cmp PER_CPU_VAR(x86_spec_ctrl_current), %rdx
	je \label
	movl %edx, %eax
	shr $32, %rdx
#else
	mov \guest_spec_ctrl, %eax
	mov PER_CPU_VAR(x86_spec_ctrl_current), %ecx
	xor %eax, %ecx
	mov 4 + \guest_spec_ctrl, %edx
	mov PER_CPU_VAR(x86_spec_ctrl_current + 4), %esi
	xor %edx, %esi
	or %esi, %ecx
	je \label
#endif
	mov $MSR_IA32_SPEC_CTRL, %ecx
	wrmsr
.endm

.macro RESTORE_HOST_SPEC_CTRL_BODY guest_spec_ctrl:req, enter_flags:req, label:req
	/* Same for after vmexit.  */
	mov $MSR_IA32_SPEC_CTRL, %ecx

	/*
	 * Load the value that the guest had written into MSR_IA32_SPEC_CTRL,
	 * if it was not intercepted during guest execution.
	 */
	testl $KVM_ENTER_SAVE_SPEC_CTRL, \enter_flags
	jz 998f
	rdmsr
	movl %eax, \guest_spec_ctrl
	movl %edx, 4 + \guest_spec_ctrl
998:
	/* Now restore the host value of the MSR if different from the guest's.  */
#ifdef CONFIG_X86_64
	mov PER_CPU_VAR(x86_spec_ctrl_current), %rdx
	cmp \guest_spec_ctrl, %rdx
	je \label
	movl %edx, %eax
	shr $32, %rdx
#else
	mov PER_CPU_VAR(x86_spec_ctrl_current), %eax
	mov \guest_spec_ctrl, %esi
	xor %eax, %esi
	mov PER_CPU_VAR(x86_spec_ctrl_current + 4), %edx
	mov 4 + \guest_spec_ctrl, %edi
	xor %edx, %edi
	or %edi, %esi
	je \label
#endif
	wrmsr
.endm

#endif /* __ASSEMBLER__ */
#endif /* __KVM_X86_VMENTER_H */
