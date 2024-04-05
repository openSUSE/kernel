/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUFEATURE_H
#define _ASM_X86_CPUFEATURE_H

#include <asm/processor.h>

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

#include <asm/asm.h>
#include <linux/bitops.h>
#include <asm/alternative.h>

enum cpuid_leafs
{
	CPUID_1_EDX		= 0,
	CPUID_8000_0001_EDX,
	CPUID_8086_0001_EDX,
	CPUID_LNX_1,
	CPUID_1_ECX,
	CPUID_C000_0001_EDX,
	CPUID_8000_0001_ECX,
	CPUID_LNX_2,
	CPUID_LNX_3,
	CPUID_7_0_EBX,
	CPUID_D_1_EAX,
	CPUID_LNX_4,
	CPUID_7_1_EAX,
	CPUID_8000_0008_EBX,
	CPUID_6_EAX,
	CPUID_8000_000A_EDX,
	CPUID_7_ECX,
	CPUID_8000_0007_EBX,
	CPUID_7_EDX,
	CPUID_8000_001F_EAX,
	CPUID_8000_0021_EAX,
	CPUID_MAX = CPUID_8000_0021_EAX,
	/*
	 * Everything below should go into the extended caps array to preserve
	 * kABI
	 */
	CPUID_LNX_5,
	CPUID_LNX_6,
};

#define CPUID_IDX(x) \
	__builtin_choose_expr((x) > CPUID_MAX, (x) - CPUID_MAX - 1, (x))
#define IS_EXT_CPUID_BIT(bit) ((bit>>5) >= NCAPINTS)

#ifdef CONFIG_X86_FEATURE_NAMES
extern const char * const x86_cap_flags[NCAPINTS*32];
extern const char * const x86_power_flags[32];
#define X86_CAP_FMT "%s"
#define x86_cap_flag(flag) x86_cap_flags[flag]
#else
#define X86_CAP_FMT "%d:%d"
#define x86_cap_flag(flag) ((flag) >> 5), ((flag) & 31)
#endif

/*
 * In order to save room, we index into this array by doing
 * X86_BUG_<name> - NCAPINTS*32.
 */
extern const char * const x86_bug_flags[(NBUGINTS+NEXTBUGINTS)*32];

/*
 * This macro must be called with bit belonging to one of the extended bug bits,
 * this required because the first bug word (19) aliases with the first extended
 * cap word (19) as such the check would be ambiguous if this macro is called
 * with a bit which represents an extended cpuid.
 */
#define IS_EXT_BUG_BIT(bit) ((bit>>5) >= (NCAPINTS+NBUGINTS))

#define test_cpu_cap(c, bit)						\
	 (IS_EXT_CPUID_BIT(bit) ? test_bit((bit) - (NCAPINTS*32), \
				(unsigned long *)((c)->x86_ext_capability)) : \
				test_bit(bit, (unsigned long *)((c)->x86_capability)))

/*
 * There are 32 bits/features in each mask word.  The high bits
 * (selected with (bit>>5) give us the word number and the low 5
 * bits give us the bit/feature number inside the word.
 * (1UL<<((bit)&31) gives us a mask for the feature_bit so we can
 * see if it is set in the mask word.
 */
#define CHECK_BIT_IN_MASK_WORD(maskname, word, bit)	\
	(((bit)>>5)==(word) && (1UL<<((bit)&31) & maskname##word ))

/*
 * {REQUIRED,DISABLED}_MASK_CHECK below may seem duplicated with the
 * following BUILD_BUG_ON_ZERO() check but when NCAPINTS gets changed, all
 * header macros which use NCAPINTS need to be changed. The duplicated macro
 * use causes the compiler to issue errors for all headers so that all usage
 * sites can be corrected.
 */
#define REQUIRED_MASK_BIT_SET(feature_bit)		\
	 ( CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  0, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  1, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  2, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  3, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  4, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  5, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  6, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  7, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  8, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK,  9, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 10, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 11, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 12, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 13, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 14, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 15, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 16, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 17, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 18, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 19, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 20, feature_bit) ||	\
	   REQUIRED_MASK_CHECK					  ||	\
	   BUILD_BUG_ON_ZERO(NCAPINTS != 22))

#define DISABLED_MASK_BIT_SET(feature_bit)				\
	 ( CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  0, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  1, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  2, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  3, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  4, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  5, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  6, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  7, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  8, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK,  9, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 10, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 11, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 12, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 13, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 14, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 15, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 16, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 17, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 18, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 19, feature_bit) ||	\
	   CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 20, feature_bit) ||	\
	   DISABLED_MASK_CHECK					  ||	\
	   BUILD_BUG_ON_ZERO(NCAPINTS != 22))

#define cpu_has(c, bit)							\
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? 1 :	\
	 test_cpu_cap(c, bit))

#define this_cpu_has(bit)						\
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? 1 :	\
	 x86_this_cpu_test_bit(bit,					\
		(unsigned long __percpu *)&cpu_info.x86_capability))

/*
 * This macro is for detection of features which need kernel
 * infrastructure to be used.  It may *not* directly test the CPU
 * itself.  Use the cpu_has() family if you want true runtime
 * testing of CPU features, like in hypervisor code where you are
 * supporting a possible guest feature where host support for it
 * is not relevant.
 */
#define cpu_feature_enabled(bit)	\
	(__builtin_constant_p(bit) && DISABLED_MASK_BIT_SET(bit) ? 0 : static_cpu_has(bit))

#define boot_cpu_has(bit)	cpu_has(&boot_cpu_data, bit)

#define set_cpu_cap(c, bit) do { \
	if (IS_EXT_CPUID_BIT(bit)) {					      \
		set_bit(bit - (NCAPINTS*32),                                  \
			(unsigned long *)(c)->x86_ext_capability);	      \
	} else {							      \
		set_bit(bit, (unsigned long *)((c)->x86_capability));	      \
	}								      \
} while (0)


extern void setup_clear_cpu_cap(unsigned int bit);
extern void clear_cpu_cap(struct cpuinfo_x86 *c, unsigned int bit);

#define setup_force_cpu_cap(bit) do { \
	if (IS_EXT_CPUID_BIT(bit)) {					      \
		set_bit(bit - (NCAPINTS*32),						      \
			(unsigned long *)&cpu_caps_set[NCAPINTS + NBUGINTS]); \
		set_bit(bit - (NCAPINTS*32),                                 \
			(unsigned long *)(&boot_cpu_data)->x86_ext_capability); \
	} else {							      \
		set_cpu_cap(&boot_cpu_data, bit);			      \
		set_bit(bit, (unsigned long *)cpu_caps_set);		      \
	}								      \
} while (0)


/*
 * This has to be re-implemented because of the aliasing issues between
 * extended capability bits and bug bits, see comment above IS_EXT_BUG_BIT.
 */
#define setup_force_cpu_bug(bit) do { \
	if (IS_EXT_BUG_BIT(bit)) {					      \
		set_bit(bit - ((NCAPINTS+NBUGINTS)*32),		      \
			(unsigned long *)&cpu_caps_set[NCAPINTS + NBUGINTS + NEXTCAPINTS]); \
		set_bit(bit - ((NCAPINTS+NBUGINTS)*32),                                 \
			(unsigned long *)&(&boot_cpu_data)->x86_ext_capability[NEXTCAPINTS]); \
	} else {							      \
		set_cpu_cap(&boot_cpu_data, bit);			      \
		set_bit(bit, (unsigned long *)cpu_caps_set);		      \
	}								      \
} while (0)

#if defined(__clang__) && !defined(CONFIG_CC_HAS_ASM_GOTO)

/*
 * Workaround for the sake of BPF compilation which utilizes kernel
 * headers, but clang does not support ASM GOTO and fails the build.
 */
#ifndef __BPF_TRACING__
#warning "Compiler lacks ASM_GOTO support. Add -D __BPF_TRACING__ to your compiler arguments"
#endif

#define static_cpu_has(bit)            boot_cpu_has(bit)

#else

/*
 * Static testing of CPU features. Used the same as boot_cpu_has(). It
 * statically patches the target code for additional performance. Use
 * static_cpu_has() only in fast paths, where every cycle counts. Which
 * means that the boot_cpu_has() variant is already fast enough for the
 * majority of cases and you should stick to using it as it is generally
 * only two instructions: a RIP-relative MOV and a TEST.
 */
static __always_inline bool _static_cpu_has(u16 bit)
{
	asm_volatile_goto(
		ALTERNATIVE_TERNARY("jmp 6f", %P[feature], "", "jmp %l[t_no]")
		".section .altinstr_aux,\"ax\"\n"
		"6:\n"
		" testb %[bitnum],%[cap_byte]\n"
		" jnz %l[t_yes]\n"
		" jmp %l[t_no]\n"
		".previous\n"
		 : : [feature]  "i" (bit),
		     [bitnum]   "i" (1 << (bit & 7)),
		     [cap_byte] "m" (((const char *)boot_cpu_data.x86_capability)[bit >> 3])
		 : : t_yes, t_no);
t_yes:
	return true;
t_no:
	return false;
}

#define static_cpu_has(bit)					\
(								\
	__builtin_constant_p(boot_cpu_has(bit)) ?		\
		boot_cpu_has(bit) :				\
		_static_cpu_has(bit)				\
)
#endif

#define cpu_has_bug(c, bit) (IS_EXT_BUG_BIT((bit)) ? test_bit((bit) - ((NCAPINTS+NBUGINTS)*32), \
				(unsigned long *)(&((c)->x86_ext_capability[NEXTCAPINTS]))) : \
				test_bit(bit, (unsigned long *)((c)->x86_capability)))

#define set_cpu_bug(c, bit) do {						\
	if (IS_EXT_BUG_BIT(bit)) {						\
		set_bit(bit - ((NCAPINTS+NBUGINTS)*32),                         \
			(unsigned long *)(&((c)->x86_ext_capability[NEXTCAPINTS]))); \
	} else {								\
		set_bit(bit, (unsigned long *)((c)->x86_capability));		\
	}									\
} while (0)

#define clear_cpu_bug(c, bit)		clear_cpu_cap(c, (bit))

#define static_cpu_has_bug(bit)		static_cpu_has((bit))
#define boot_cpu_has_bug(bit)		cpu_has_bug(&boot_cpu_data, (bit))
#define boot_cpu_set_bug(bit)		set_cpu_bug(&boot_cpu_data, (bit))

#define MAX_CPU_FEATURES		((NCAPINTS + NEXTCAPINTS)* 32)
#define cpu_have_feature		boot_cpu_has

#define CPU_FEATURE_TYPEFMT		"x86,ven%04Xfam%04Xmod%04X"
#define CPU_FEATURE_TYPEVAL		boot_cpu_data.x86_vendor, boot_cpu_data.x86, \
					boot_cpu_data.x86_model

#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */
#endif /* _ASM_X86_CPUFEATURE_H */
