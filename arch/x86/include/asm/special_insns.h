/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H

#ifdef __KERNEL__
#include <asm/nops.h>
#include <asm/processor-flags.h>

#include <linux/errno.h>
#include <linux/irqflags.h>
#include <linux/jump_label.h>

void native_write_cr0(unsigned long val);

static inline unsigned long native_read_cr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0" : "=r" (val));
	return val;
}

static __always_inline unsigned long native_read_cr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0" : "=r" (val));
	return val;
}

static __always_inline void native_write_cr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2": : "r" (val) : "memory");
}

static __always_inline unsigned long __native_read_cr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0" : "=r" (val));
	return val;
}

static __always_inline void native_write_cr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3": : "r" (val) : "memory");
}

static inline unsigned long native_read_cr4(void)
{
	unsigned long val;
#ifdef CONFIG_X86_32
	/*
	 * This could fault if CR4 does not exist.  Non-existent CR4
	 * is functionally equivalent to CR4 == 0.  Keep it simple and pretend
	 * that CR4 == 0 on CPUs that don't have CR4.
	 */
	asm volatile("1: mov %%cr4, %0\n"
		     "2:\n"
		     _ASM_EXTABLE(1b, 2b)
		     : "=r" (val) : "0" (0));
#else
	/* CR4 always exists on x86_64. */
	asm volatile("mov %%cr4,%0" : "=r" (val));
#endif
	return val;
}

void native_write_cr4(unsigned long val);

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
static inline u32 rdpkru(void)
{
	u32 ecx = 0;
	u32 edx, pkru;

	/*
	 * "rdpkru" instruction.  Places PKRU contents in to EAX,
	 * clears EDX and requires that ecx=0.
	 */
	asm volatile("rdpkru" : "=a" (pkru), "=d" (edx) : "c" (ecx));
	return pkru;
}

static inline void wrpkru(u32 pkru)
{
	u32 ecx = 0, edx = 0;

	/*
	 * "wrpkru" instruction.  Loads contents in EAX to PKRU,
	 * requires that ecx = edx = 0.
	 */
	asm volatile("wrpkru" : : "a" (pkru), "c"(ecx), "d"(edx));
}

#else
static inline u32 rdpkru(void)
{
	return 0;
}

static inline void wrpkru(u32 pkru)
{
}
#endif

/*
 * Write back all modified lines in all levels of cache associated with this
 * logical processor to main memory, and then invalidate all caches.  Depending
 * on the micro-architecture, WBINVD (and WBNOINVD below) may or may not affect
 * lower level caches associated with another logical processor that shares any
 * level of this processor's cache hierarchy.
 */
static __always_inline void wbinvd(void)
{
	asm volatile("wbinvd" : : : "memory");
}

/* Instruction encoding provided for binutils backwards compatibility. */
#define ASM_WBNOINVD _ASM_BYTES(0xf3,0x0f,0x09)

/*
 * Write back all modified lines in all levels of cache associated with this
 * logical processor to main memory, but do NOT explicitly invalidate caches,
 * i.e. leave all/most cache lines in the hierarchy in non-modified state.
 */
static __always_inline void wbnoinvd(void)
{
	/*
	 * Explicitly encode WBINVD if X86_FEATURE_WBNOINVD is unavailable even
	 * though WBNOINVD is backwards compatible (it's simply WBINVD with an
	 * ignored REP prefix), to guarantee that WBNOINVD isn't used if it
	 * needs to be avoided for any reason.  For all supported usage in the
	 * kernel, WBINVD is functionally a superset of WBNOINVD.
	 */
	alternative("wbinvd", ASM_WBNOINVD, X86_FEATURE_WBNOINVD);
}

static inline unsigned long __read_cr4(void)
{
	return native_read_cr4();
}

#ifdef CONFIG_PARAVIRT_XXL
#include <asm/paravirt.h>
#else

static inline unsigned long read_cr0(void)
{
	return native_read_cr0();
}

static inline void write_cr0(unsigned long x)
{
	native_write_cr0(x);
}

static __always_inline unsigned long read_cr2(void)
{
	return native_read_cr2();
}

static __always_inline void write_cr2(unsigned long x)
{
	native_write_cr2(x);
}

/*
 * Careful!  CR3 contains more than just an address.  You probably want
 * read_cr3_pa() instead.
 */
static inline unsigned long __read_cr3(void)
{
	return __native_read_cr3();
}

static inline void write_cr3(unsigned long x)
{
	native_write_cr3(x);
}

static inline void __write_cr4(unsigned long x)
{
	native_write_cr4(x);
}
#endif /* CONFIG_PARAVIRT_XXL */

static __always_inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m" (*(volatile char __force *)__p));
}

static inline void clflushopt(volatile void *__p)
{
	alternative_io("ds clflush %0",
		       "clflushopt %0", X86_FEATURE_CLFLUSHOPT,
		       "+m" (*(volatile char __force *)__p));
}

static inline void clwb(volatile void *__p)
{
	volatile struct { char x[64]; } *p = __p;

	asm_inline volatile(ALTERNATIVE_2(
		"ds clflush %0",
		"clflushopt %0", X86_FEATURE_CLFLUSHOPT,
		"clwb %0", X86_FEATURE_CLWB)
		: "+m" (*p));
}

#ifdef CONFIG_X86_USER_SHADOW_STACK
static inline int write_user_shstk_64(u64 __user *addr, u64 val)
{
	asm goto("1: wrussq %[val], %[addr]\n"
			  _ASM_EXTABLE(1b, %l[fail])
			  :: [addr] "m" (*addr), [val] "r" (val)
			  :: fail);
	return 0;
fail:
	return -EFAULT;
}
#endif /* CONFIG_X86_USER_SHADOW_STACK */

#define nop() asm volatile ("nop")

static __always_inline void serialize(void)
{
	/* Instruction opcode for SERIALIZE; supported in binutils >= 2.35. */
	asm volatile(".byte 0xf, 0x1, 0xe8" ::: "memory");
}

/* The dst parameter must be 64-bytes aligned */
static inline void movdir64b(void *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } *__dst = dst;

	/*
	 * MOVDIR64B %(rdx), rax.
	 *
	 * Both __src and __dst must be memory constraints in order to tell the
	 * compiler that no other memory accesses should be reordered around
	 * this one.
	 *
	 * Also, both must be supplied as lvalues because this tells
	 * the compiler what the object is (its size) the instruction accesses.
	 * I.e., not the pointers but what they point to, thus the deref'ing '*'.
	 */
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02"
		     : "+m" (*__dst)
		     :  "m" (*__src), "a" (__dst), "d" (__src));
}

static inline void movdir64b_io(void __iomem *dst, const void *src)
{
	movdir64b((void __force *)dst, src);
}

/**
 * enqcmds - Enqueue a command in supervisor (CPL0) mode
 * @dst: destination, in MMIO space (must be 512-bit aligned)
 * @src: 512 bits memory operand
 *
 * The ENQCMDS instruction allows software to write a 512-bit command to
 * a 512-bit-aligned special MMIO region that supports the instruction.
 * A return status is loaded into the ZF flag in the RFLAGS register.
 * ZF = 0 equates to success, and ZF = 1 indicates retry or error.
 *
 * This function issues the ENQCMDS instruction to submit data from
 * kernel space to MMIO space, in a unit of 512 bits. Order of data access
 * is not guaranteed, nor is a memory barrier performed afterwards. It
 * returns 0 on success and -EAGAIN on failure.
 *
 * Warning: Do not use this helper unless your driver has checked that the
 * ENQCMDS instruction is supported on the platform and the device accepts
 * ENQCMDS.
 */
static inline int enqcmds(void __iomem *dst, const void *src)
{
	const struct { char _[64]; } *__src = src;
	struct { char _[64]; } __iomem *__dst = dst;
	bool zf;

	/*
	 * ENQCMDS %(rdx), rax
	 *
	 * See movdir64b()'s comment on operand specification.
	 */
	asm volatile(".byte 0xf3, 0x0f, 0x38, 0xf8, 0x02, 0x66, 0x90"
		     : "=@ccz" (zf), "+m" (*__dst)
		     : "m" (*__src), "a" (__dst), "d" (__src));

	/* Submission failure is indicated via EFLAGS.ZF=1 */
	if (zf)
		return -EAGAIN;

	return 0;
}

static __always_inline void tile_release(void)
{
	/*
	 * Instruction opcode for TILERELEASE; supported in binutils
	 * version >= 2.36.
	 */
	asm volatile(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0");
}

#endif /* __KERNEL__ */

#endif /* _ASM_X86_SPECIAL_INSNS_H */
