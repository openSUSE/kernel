/*
 * arch/ppc/boot/simple/misc-radstone_ppc7d.c
 *
 * Misc data for Radstone PPC7D board.
 *
 * Author: James Chapman <jchapman@katalix.com>
 */

#include <linux/types.h>
#include <asm/reg.h>

#include "../../platforms/radstone_ppc7d.h"

#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
long	mv64x60_mpsc_clk_freq = PPC7D_MPSC_CLK_FREQ;;
long	mv64x60_mpsc_clk_src = PPC7D_MPSC_CLK_SRC;
long	mv64x60_mpsc_console_baud = PPC7D_DEFAULT_BAUD;
#endif

