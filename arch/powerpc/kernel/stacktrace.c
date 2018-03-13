/*
 * Stack trace utility
 *
 * Copyright 2008 Christoph Hellwig, IBM Corp.
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
static void save_context_stack(struct stack_trace *trace, unsigned long sp,
			struct task_struct *tsk, int savesched)
{
	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		if (!validate_sp(sp, tsk, STACK_FRAME_OVERHEAD))
			return;

		newsp = stack[0];
		ip = stack[STACK_FRAME_LR_SAVE];

		if (savesched || !in_sched_functions(ip)) {
			if (!trace->skip)
				trace->entries[trace->nr_entries++] = ip;
			else
				trace->skip--;
		}

		if (trace->nr_entries >= trace->max_entries)
			return;

		sp = newsp;
	}
}

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long sp;

	sp = current_stack_pointer();

	save_context_stack(trace, sp, current, 1);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long sp;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	save_context_stack(trace, sp, tsk, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	save_context_stack(trace, regs->gpr[1], current, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);

#ifdef CONFIG_HAVE_RELIABLE_STACKTRACE
int
save_stack_trace_tsk_reliable(struct task_struct *tsk,
				struct stack_trace *trace)
{
	unsigned long sp;
	unsigned long stack_page = (unsigned long)task_stack_page(tsk);

	/* The last frame (unwinding first) may not yet have saved
	 * its LR onto the stack.
	 */
	int firstframe = 1;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	if (sp < stack_page + sizeof(struct thread_struct)
	    || sp > stack_page + THREAD_SIZE - STACK_FRAME_OVERHEAD)
		return 1;

	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		/* sanity check: ABI requires SP to be aligned 16 bytes. */
		if (sp & 0xF)
			return 1;

		newsp = stack[0];
		/* Stack grows downwards; unwinder may only go up. */
		if (newsp <= sp)
			return 1;

		if (newsp >= stack_page + THREAD_SIZE)
			return 1; /* invalid backlink, too far up. */

		/* Examine the saved LR: it must point into kernel code. */
		ip = stack[STACK_FRAME_LR_SAVE];
		if (!firstframe) {
			if (!func_ptr_is_kernel_text((void *)ip)) {
#ifdef CONFIG_MODULES
				struct module *mod = __module_text_address(ip);

				if (!mod)
#endif
					return 1;
			}
		}
		firstframe = 0;

		if (!trace->skip)
			trace->entries[trace->nr_entries++] = ip;
		else
			trace->skip--;

		/* SP value loaded on kernel entry, see "PACAKSAVE(r13)" in
		 * _switch() and system_call_common()
		 */
		if (newsp == stack_page + THREAD_SIZE - /* SWITCH_FRAME_SIZE */
		    (STACK_FRAME_OVERHEAD + sizeof(struct pt_regs)))
			break;

		if (trace->nr_entries >= trace->max_entries)
			return -E2BIG;

		sp = newsp;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk_reliable);
#endif /* CONFIG_HAVE_RELIABLE_STACKTRACE */
