/*
 * OMAP Power Management debug routines
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Written by:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Tony Lindgren
 * Juha Yrjola
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Igor Stoppa <igor.stoppa@nokia.com>
 * Jouni Hogander
 *
 * Based on pm.c for omap2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <plat/clock.h>
#include <plat/board.h>
#include "powerdomain.h"
#include "clockdomain.h"
#include <plat/dmtimer.h>
#include <plat/omap-pm.h>

#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#include "pm.h"

u32 enable_off_mode;

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int pm_dbg_init_done;

static int pm_dbg_init(void);

enum {
	DEBUG_FILE_COUNTERS = 0,
	DEBUG_FILE_TIMERS,
	DEBUG_FILE_USECOUNT,
};

static const char pwrdm_state_names[][PWRDM_MAX_PWRSTS] = {
	"OFF",
	"RET",
	"INA",
	"ON"
};

void pm_dbg_update_time(struct powerdomain *pwrdm, int prev)
{
	s64 t;

	if (!pm_dbg_init_done)
		return ;

	/* Update timer for previous state */
	t = sched_clock();

	pwrdm->state_timer[prev] += t - pwrdm->timer;

	pwrdm->timer = t;
}

static int pwrdm_dbg_show_counter(struct powerdomain *pwrdm, void *user)
{
	struct seq_file *s = (struct seq_file *)user;
	int i;

	if (strcmp(pwrdm->name, "emu_pwrdm") == 0 ||
		strcmp(pwrdm->name, "wkup_pwrdm") == 0 ||
		strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	if (pwrdm->state != pwrdm_read_pwrst(pwrdm))
		printk(KERN_ERR "pwrdm state mismatch(%s) %d != %d\n",
			pwrdm->name, pwrdm->state, pwrdm_read_pwrst(pwrdm));

	seq_printf(s, "%s (%s)", pwrdm->name,
			pwrdm_state_names[pwrdm->state]);
	for (i = 0; i < PWRDM_MAX_PWRSTS; i++)
		seq_printf(s, ",%s:%d", pwrdm_state_names[i],
			pwrdm->state_counter[i]);

	seq_printf(s, ",RET-LOGIC-OFF:%d", pwrdm->ret_logic_off_counter);
	for (i = 0; i < pwrdm->banks; i++)
		seq_printf(s, ",RET-MEMBANK%d-OFF:%d", i + 1,
				pwrdm->ret_mem_off_counter[i]);

	seq_printf(s, "\n");

	return 0;
}

static int pwrdm_dbg_show_timer(struct powerdomain *pwrdm, void *user)
{
	struct seq_file *s = (struct seq_file *)user;
	int i;

	if (strcmp(pwrdm->name, "emu_pwrdm") == 0 ||
		strcmp(pwrdm->name, "wkup_pwrdm") == 0 ||
		strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	pwrdm_state_switch(pwrdm);

	seq_printf(s, "%s (%s)", pwrdm->name,
		pwrdm_state_names[pwrdm->state]);

	for (i = 0; i < 4; i++)
		seq_printf(s, ",%s:%lld", pwrdm_state_names[i],
			pwrdm->state_timer[i]);

	seq_printf(s, "\n");
	return 0;
}

static struct voltagedomain *parent_voltdm;
static struct powerdomain *parent_pwrdm;
static struct clockdomain *parent_clkdm;

#define PM_DBG_PRINT(s, fmt, args...)			\
	{						\
		if (s)					\
			seq_printf(s, fmt, ## args);	\
		else					\
			pr_info(fmt, ## args);		\
	}

static int _pm_dbg_dump_clk(struct clk *clk, void *user)
{
	struct seq_file *s = user;

	if (clk->clkdm == parent_clkdm && clk->usecount && !clk->autoidle)
		PM_DBG_PRINT(s, "      ck:%s: %d\n", clk->name, clk->usecount);

	return 0;
}

static int _pm_dbg_dump_hwmod(struct omap_hwmod *oh, void *user)
{
	struct seq_file *s = user;

	if (oh->clkdm != parent_clkdm)
		return 0;

	if (oh->_state != _HWMOD_STATE_ENABLED)
		return 0;

	PM_DBG_PRINT(s, "      oh:%s: enabled\n", oh->name);

	return 0;
}

static int _pm_dbg_dump_clkdm(struct clockdomain *clkdm, void *user)
{
	struct seq_file *s = user;
	u32 usecount;

	if (clkdm->pwrdm.ptr == parent_pwrdm) {
		usecount = atomic_read(&clkdm->usecount);
		if (usecount) {
			PM_DBG_PRINT(s, "    cd:%s: %d\n", clkdm->name,
				usecount);
			parent_clkdm = clkdm;
			omap_hwmod_for_each(_pm_dbg_dump_hwmod, s);
			omap_clk_for_each(_pm_dbg_dump_clk, s);
		}
	}
	return 0;
}

static int _pm_dbg_dump_pwrdm(struct powerdomain *pwrdm, void *user)
{
	struct seq_file *s = user;
	u32 usecount;

	if (pwrdm->voltdm.ptr == parent_voltdm) {
		usecount = atomic_read(&pwrdm->usecount);
		if (usecount) {
			PM_DBG_PRINT(s, "  pd:%s: %d\n", pwrdm->name, usecount);
			parent_pwrdm = pwrdm;
			clkdm_for_each(_pm_dbg_dump_clkdm, s);
		}
	}
	return 0;
}

void pm_dbg_dump_pwrdm(struct powerdomain *pwrdm)
{
	pr_info("pd:%s: %d\n", pwrdm->name, atomic_read(&pwrdm->usecount));
	parent_pwrdm = pwrdm;
	clkdm_for_each(_pm_dbg_dump_clkdm, NULL);
}

void pm_dbg_dump_voltdm(struct voltagedomain *voltdm)
{
	pr_info("vd:%s: %d\n", voltdm->name, atomic_read(&voltdm->usecount));
	parent_voltdm = voltdm;
	pwrdm_for_each(_pm_dbg_dump_pwrdm, NULL);
}

static int _voltdm_dbg_show_counters(struct voltagedomain *voltdm, void *user)
{
	struct seq_file *s = user;

	seq_printf(s, "vd:%s: %d\n", voltdm->name,
		atomic_read(&voltdm->usecount));

	parent_voltdm = voltdm;
	pwrdm_for_each(_pm_dbg_dump_pwrdm, s);
	return 0;
}

static int pm_dbg_show_usecount(struct seq_file *s, void *unused)
{
	voltdm_for_each(_voltdm_dbg_show_counters, s);
	return 0;
}

static int pm_dbg_show_counters(struct seq_file *s, void *unused)
{
	pwrdm_for_each(pwrdm_dbg_show_counter, s);
	return 0;
}

static int pm_dbg_show_timers(struct seq_file *s, void *unused)
{
	pwrdm_for_each(pwrdm_dbg_show_timer, s);
	return 0;
}

static int pm_dbg_open(struct inode *inode, struct file *file)
{
	switch ((int)inode->i_private) {
	case DEBUG_FILE_USECOUNT:
		return single_open(file, pm_dbg_show_usecount,
			&inode->i_private);
	case DEBUG_FILE_COUNTERS:
		return single_open(file, pm_dbg_show_counters,
			&inode->i_private);
	case DEBUG_FILE_TIMERS:
	default:
		return single_open(file, pm_dbg_show_timers,
			&inode->i_private);
	};
}

static const struct file_operations debug_fops = {
	.open           = pm_dbg_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int pwrdm_suspend_get(void *data, u64 *val)
{
	int ret = -EINVAL;

	if (cpu_is_omap34xx())
		ret = omap3_pm_get_suspend_state((struct powerdomain *)data);
	*val = ret;

	if (ret >= 0)
		return 0;
	return *val;
}

static int pwrdm_suspend_set(void *data, u64 val)
{
	if (cpu_is_omap34xx())
		return omap3_pm_set_suspend_state(
			(struct powerdomain *)data, (int)val);
	return -EINVAL;
}

DEFINE_SIMPLE_ATTRIBUTE(pwrdm_suspend_fops, pwrdm_suspend_get,
			pwrdm_suspend_set, "%llu\n");

static int __init pwrdms_setup(struct powerdomain *pwrdm, void *dir)
{
	int i;
	s64 t;
	struct dentry *d;

	t = sched_clock();

	for (i = 0; i < 4; i++)
		pwrdm->state_timer[i] = 0;

	pwrdm->timer = t;

	if (strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	d = debugfs_create_dir(pwrdm->name, (struct dentry *)dir);
	if (!(IS_ERR_OR_NULL(d)))
		(void) debugfs_create_file("suspend", S_IRUGO|S_IWUSR, d,
			(void *)pwrdm, &pwrdm_suspend_fops);

	return 0;
}

static int option_get(void *data, u64 *val)
{
	u32 *option = data;

	*val = *option;

	return 0;
}

static int option_set(void *data, u64 val)
{
	u32 *option = data;

	*option = val;

	if (option == &enable_off_mode) {
		if (val)
			omap_pm_enable_off_mode();
		else
			omap_pm_disable_off_mode();
		if (cpu_is_omap34xx())
			omap3_pm_off_mode_enable(val);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(pm_dbg_option_fops, option_get, option_set, "%llu\n");

static int __init pm_dbg_init(void)
{
	struct dentry *d;

	if (pm_dbg_init_done)
		return 0;

	d = debugfs_create_dir("pm_debug", NULL);
	if (IS_ERR_OR_NULL(d))
		return PTR_ERR(d);

	(void) debugfs_create_file("count", S_IRUGO,
		d, (void *)DEBUG_FILE_COUNTERS, &debug_fops);
	(void) debugfs_create_file("time", S_IRUGO,
		d, (void *)DEBUG_FILE_TIMERS, &debug_fops);
	(void) debugfs_create_file("usecount", S_IRUGO,
		d, (void *)DEBUG_FILE_USECOUNT, &debug_fops);

	pwrdm_for_each(pwrdms_setup, (void *)d);

	(void) debugfs_create_file("enable_off_mode", S_IRUGO | S_IWUSR, d,
				   &enable_off_mode, &pm_dbg_option_fops);
	pm_dbg_init_done = 1;

	return 0;
}
arch_initcall(pm_dbg_init);

#endif
