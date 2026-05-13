// SPDX-License-Identifier: GPL-2.0
/*
 * This file setups defines to compile arch specific binary from the
 * generic one.
 *
 * The function 'LIBUNWIND__ARCH_REG_ID' name is set according to arch
 * name and the definition of this function is included directly from
 * 'arch/x86/util/unwind-libunwind.c', to make sure that this function
 * is defined no matter what arch the host is.
 *
 * Finally, the arch specific unwind methods are exported which will
 * be assigned to each x86 thread.
 */

#define REMOTE_UNWIND_LIBUNWIND

#include "unwind.h"
#include "libunwind-x86.h"

/* Explicitly define NO_LIBUNWIND_DEBUG_FRAME, because non-ARM has no
 * dwarf_find_debug_frame() function.
 */
#ifndef NO_LIBUNWIND_DEBUG_FRAME
#define NO_LIBUNWIND_DEBUG_FRAME
#endif
#include "util/unwind-libunwind-local.c"

struct unwind_libunwind_ops *
x86_32_unwind_libunwind_ops = &_unwind_libunwind_ops;
