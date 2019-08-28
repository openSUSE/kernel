#ifndef _ASM_POWERPC_SETUP_H
#define _ASM_POWERPC_SETUP_H

#include <asm-generic/setup.h>

#ifndef __ASSEMBLY__

extern bool init_mem_is_free;

void check_for_initrd(void);
void do_init_bootmem(void);
void setup_panic(void);

void rfi_flush_enable(bool enable);

/* These are bit flags */
enum l1d_flush_type {
	L1D_FLUSH_NONE		= 0x1,
	L1D_FLUSH_FALLBACK	= 0x2,
	L1D_FLUSH_ORI		= 0x4,
	L1D_FLUSH_MTTRIG	= 0x8,
};

void setup_rfi_flush(enum l1d_flush_type, bool enable);
void do_rfi_flush_fixups(enum l1d_flush_type types);
void setup_barrier_nospec(void);
void do_barrier_nospec_fixups(bool enable);
extern bool barrier_nospec_enabled;

#ifdef CONFIG_PPC_BOOK3S_64
void do_barrier_nospec_fixups_range(bool enable, void *start, void *end);
#else
static inline void do_barrier_nospec_fixups_range(bool enable, void *start, void *end) { };
#endif

#ifdef CONFIG_PPC_FSL_BOOK3E
void setup_spectre_v2(void);
#else
static inline void setup_spectre_v2(void) {};
#endif

#endif /* !__ASSEMBLY__ */

#endif	/* _ASM_POWERPC_SETUP_H */
