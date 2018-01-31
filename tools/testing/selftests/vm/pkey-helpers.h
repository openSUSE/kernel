/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PKEYS_HELPER_H
#define _PKEYS_HELPER_H
#define _GNU_SOURCE
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/mman.h>

/* Define some kernel-like types */
#define  u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define pkey_reg_t u32

#ifdef __i386__
#define SYS_mprotect_key 380
#define SYS_pkey_alloc	 381
#define SYS_pkey_free	 382
#define REG_IP_IDX REG_EIP
#define si_pkey_offset 0x14
#else
#define SYS_mprotect_key 329
#define SYS_pkey_alloc	 330
#define SYS_pkey_free	 331
#define REG_IP_IDX REG_RIP
#define si_pkey_offset 0x20
#endif

#define NR_PKEYS 16
#define PKEY_BITS_PER_PKEY 2
#define PKEY_DISABLE_ACCESS    0x1
#define PKEY_DISABLE_WRITE     0x2
#define HPAGE_SIZE	(1UL<<21)

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif
#define DPRINT_IN_SIGNAL_BUF_SIZE 4096
extern int dprint_in_signal;
extern char dprint_in_signal_buffer[DPRINT_IN_SIGNAL_BUF_SIZE];
static inline void sigsafe_printf(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	if (!dprint_in_signal) {
		vprintf(format, ap);
	} else {
		int ret;
		int len = vsnprintf(dprint_in_signal_buffer,
				    DPRINT_IN_SIGNAL_BUF_SIZE,
				    format, ap);
		/*
		 * len is amount that would have been printed,
		 * but actual write is truncated at BUF_SIZE.
		 */
		if (len > DPRINT_IN_SIGNAL_BUF_SIZE)
			len = DPRINT_IN_SIGNAL_BUF_SIZE;
		ret = write(1, dprint_in_signal_buffer, len);
		if (ret < 0)
			abort();
	}
	va_end(ap);
}
#define dprintf_level(level, args...) do {	\
	if (level <= DEBUG_LEVEL)		\
		sigsafe_printf(args);		\
	fflush(NULL);				\
} while (0)
#define dprintf0(args...) dprintf_level(0, args)
#define dprintf1(args...) dprintf_level(1, args)
#define dprintf2(args...) dprintf_level(2, args)
#define dprintf3(args...) dprintf_level(3, args)
#define dprintf4(args...) dprintf_level(4, args)

extern pkey_reg_t shadow_pkey_reg;
static inline pkey_reg_t __rdpkey_reg(void)
{
	unsigned int eax, edx;
	unsigned int ecx = 0;
	pkey_reg_t pkey_reg;

	asm volatile(".byte 0x0f,0x01,0xee\n\t"
		     : "=a" (eax), "=d" (edx)
		     : "c" (ecx));
	pkey_reg = eax;
	return pkey_reg;
}

static inline pkey_reg_t _rdpkey_reg(int line)
{
	pkey_reg_t pkey_reg = __rdpkey_reg();

	dprintf4("rdpkey_reg(line=%d) pkey_reg: %016lx shadow: %016lx\n",
			line, pkey_reg, shadow_pkey_reg);
	assert(pkey_reg == shadow_pkey_reg);

	return pkey_reg;
}

#define rdpkey_reg() _rdpkey_reg(__LINE__)

static inline void __wrpkey_reg(pkey_reg_t pkey_reg)
{
	pkey_reg_t eax = pkey_reg;
	pkey_reg_t ecx = 0;
	pkey_reg_t edx = 0;

	dprintf4("%s() changing %08x to %08x\n", __func__,
			__rdpkey_reg(), pkey_reg);
	asm volatile(".byte 0x0f,0x01,0xef\n\t"
		     : : "a" (eax), "c" (ecx), "d" (edx));
	assert(pkey_reg == __rdpkey_reg());
}

static inline void wrpkey_reg(pkey_reg_t pkey_reg)
{
	dprintf4("%s() changing %08x to %08x\n", __func__,
			__rdpkey_reg(), pkey_reg);
	/* will do the shadow check for us: */
	rdpkey_reg();
	__wrpkey_reg(pkey_reg);
	shadow_pkey_reg = pkey_reg;
	dprintf4("%s(%08x) pkey_reg: %08x\n", __func__,
			pkey_reg, __rdpkey_reg());
}

/*
 * These are technically racy. since something could
 * change PKEY register between the read and the write.
 */
static inline void __pkey_access_allow(int pkey, int do_allow)
{
	pkey_reg_t pkey_reg = rdpkey_reg();
	int bit = pkey * 2;

	if (do_allow)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	dprintf4("pkey_reg now: %08x\n", rdpkey_reg());
	wrpkey_reg(pkey_reg);
}

static inline void __pkey_write_allow(int pkey, int do_allow_write)
{
	pkey_reg_t pkey_reg = rdpkey_reg();
	int bit = pkey * 2 + 1;

	if (do_allow_write)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	wrpkey_reg(pkey_reg);
	dprintf4("pkey_reg now: %08x\n", rdpkey_reg());
}

#define PAGE_SIZE 4096
#define MB	(1<<20)

static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
		unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile(
		"cpuid;"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ecx) */
#define X86_FEATURE_PKU        (1<<3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE      (1<<4) /* OS Protection Keys Enable */

static inline int cpu_has_pku(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	eax = 0x7;
	ecx = 0x0;
	__cpuid(&eax, &ebx, &ecx, &edx);

	if (!(ecx & X86_FEATURE_PKU)) {
		dprintf2("cpu does not have PKU\n");
		return 0;
	}
	if (!(ecx & X86_FEATURE_OSPKE)) {
		dprintf2("cpu does not have OSPKE\n");
		return 0;
	}
	return 1;
}

#define XSTATE_PKEY_BIT	(9)
#define XSTATE_PKEY	0x200

int pkey_reg_xstate_offset(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	int xstate_offset;
	int xstate_size;
	unsigned long XSTATE_CPUID = 0xd;
	int leaf;

	/* assume that XSTATE_PKEY is set in XCR0 */
	leaf = XSTATE_PKEY_BIT;
	{
		eax = XSTATE_CPUID;
		ecx = leaf;
		__cpuid(&eax, &ebx, &ecx, &edx);

		if (leaf == XSTATE_PKEY_BIT) {
			xstate_offset = ebx;
			xstate_size = eax;
		}
	}

	if (xstate_size == 0) {
		printf("could not find size/offset of PKEY in xsave state\n");
		return 0;
	}

	return xstate_offset;
}

static inline void __page_o_noops(void)
{
	/* 8-bytes of instruction * 512 bytes = 1 page */
	asm(".rept 512 ; nopl 0x7eeeeeee(%eax) ; .endr");
}

#endif /* _PKEYS_HELPER_H */

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define ALIGN_UP(x, align_to)	(((x) + ((align_to)-1)) & ~((align_to)-1))
#define ALIGN_DOWN(x, align_to) ((x) & ~((align_to)-1))
#define ALIGN_PTR_UP(p, ptr_align_to)	\
		((typeof(p))ALIGN_UP((unsigned long)(p), ptr_align_to))
#define ALIGN_PTR_DOWN(p, ptr_align_to) \
	((typeof(p))ALIGN_DOWN((unsigned long)(p), ptr_align_to))
#define __stringify_1(x...)     #x
#define __stringify(x...)       __stringify_1(x)

#define PTR_ERR_ENOTSUP ((void *)-ENOTSUP)

int dprint_in_signal;
char dprint_in_signal_buffer[DPRINT_IN_SIGNAL_BUF_SIZE];

extern void abort_hooks(void);
#define pkey_assert(condition) do {		\
	if (!(condition)) {			\
		dprintf0("assert() at %s::%d test_nr: %d iteration: %d\n", \
				__FILE__, __LINE__,	\
				test_nr, iteration_nr);	\
		dprintf0("errno at assert: %d", errno);	\
		abort_hooks();			\
		assert(condition);		\
	}					\
} while (0)
#define raw_assert(cond) assert(cond)
