// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 */

/*
 * livepatch-annotated-sample.c - Kernel Live Patching Sample Module
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

/*
 * This (dumb) live patch overrides the function that prints the
 * kernel boot cmdline when /proc/cmdline is read.
 *
 * This livepatch uses the symbol saved_command_line whose relocation
 * must be resolved during load time. To enable that, this module
 * must be post-processed by a tool called klp-convert, which embeds
 * information to be used by the loader to solve the relocation.
 *
 * The module is annotated with KLP_RELOC_SYMBOL macros.
 * These annotations are used by klp-convert to infer that the symbol
 * saved_command_line is in the object vmlinux.
 *
 * Example:
 *
 * $ cat /proc/cmdline
 * <your cmdline>
 *
 * $ insmod livepatch-sample.ko
 * $ cat /proc/cmdline
 * <your cmdline> livepatch=1
 *
 * $ echo 0 > /sys/kernel/livepatch/livepatch_sample/enabled
 * $ cat /proc/cmdline
 * <your cmdline>
 */

extern char *saved_command_line \
	       KLP_RELOC_SYMBOL(vmlinux, vmlinux, saved_command_line);

#include <linux/seq_file.h>
static int livepatch_cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s livepatch=1\n", saved_command_line);
	return 0;
}

static struct klp_func funcs[] = {
	{
		.old_name = "cmdline_proc_show",
		.new_func = livepatch_cmdline_proc_show,
	}, { }
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int livepatch_init(void)
{
	return klp_enable_patch(&patch);
}

static void livepatch_exit(void)
{
}

module_init(livepatch_init);
module_exit(livepatch_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
