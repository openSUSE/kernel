/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 by Ralf Baechle
 */
#include <linux/clocksource.h>
#include <linux/cnt32_to_63.h>
#include <linux/init.h>
#include <linux/timer.h>

#include <asm/time.h>

static cycle_t c0_hpt_read(struct clocksource *cs)
{
	return read_c0_count();
}

static struct clocksource clocksource_mips = {
	.name		= "MIPS",
	.read		= c0_hpt_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_CPU_SUPPORTS_HR_SCHED_CLOCK
/*
 * MIPS sched_clock implementation.
 *
 * Because the hardware timer period is quite short and because cnt32_to_63()
 * needs to be called at least once per half period to work properly, a kernel
 * timer is set up to ensure this requirement is always met.
 *
 * Please refer to include/linux/cnt32_to_63.h, arch/arm/plat-orion/time.c and
 * arch/mips/include/asm/time.h (mips_sched_clock)
 */
unsigned long long notrace sched_clock(void)
{
	u64 cnt = cnt32_to_63(read_c0_count());

	if (cnt & 0x8000000000000000)
		cnt &= 0x7fffffffffffffff;

	return mips_sched_clock(&clocksource_mips, cnt);
}

static struct timer_list cnt32_to_63_keepwarm_timer;

static void cnt32_to_63_keepwarm(unsigned long data)
{
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + data));
	sched_clock();
}
#endif

static inline void setup_hres_sched_clock(unsigned long clock)
{
#ifdef CONFIG_CPU_SUPPORTS_HR_SCHED_CLOCK
	unsigned long data;

	data = 0x80000000UL / clock * HZ;
	setup_timer(&cnt32_to_63_keepwarm_timer, cnt32_to_63_keepwarm, data);
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + data));
#endif
}

int __init init_r4k_clocksource(void)
{
	if (!cpu_has_counter || !mips_hpt_frequency)
		return -ENXIO;

	/* Calculate a somewhat reasonable rating value */
	clocksource_mips.rating = 200 + mips_hpt_frequency / 10000000;

	clocksource_set_clock(&clocksource_mips, mips_hpt_frequency);

	setup_hres_sched_clock(mips_hpt_frequency);

	clocksource_register(&clocksource_mips);

	return 0;
}
