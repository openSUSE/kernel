// SPDX-License-Identifier: GPL-2.0
#include "unwind.h"
#include "dso.h"
#include "map.h"
#include "thread.h"
#include "session.h"
#include "debug.h"
#include "env.h"
#include "callchain.h"
#include "libunwind-arch/libunwind-arch.h"
#include <dwarf-regs.h>
#include <elf.h>

struct unwind_libunwind_ops __weak *local_unwind_libunwind_ops;
struct unwind_libunwind_ops __weak *x86_32_unwind_libunwind_ops;
struct unwind_libunwind_ops __weak *arm64_unwind_libunwind_ops;

int unwind__prepare_access(struct maps *maps, uint16_t e_machine)
{
	struct unwind_libunwind_ops *ops = local_unwind_libunwind_ops;

	if (!dwarf_callchain_users)
		return 0;

	if (maps__addr_space(maps)) {
		pr_debug3("unwind: thread map already set\n");
		return 0;
	}

	if (e_machine == EM_NONE)
		return 0;

	if (e_machine != EM_HOST) {
		/* If not live/local mode. */
		switch (e_machine) {
		case EM_386:
			ops = x86_32_unwind_libunwind_ops;
			break;
		case EM_AARCH64:
			ops = arm64_unwind_libunwind_ops;
			break;
		default:
			pr_warning_once("unwind: ELF machine type %d is not supported\n",
					e_machine);
			return 0;
		}
	}

	if (!ops) {
		pr_warning_once("unwind: target platform is not supported\n");
		return 0;
	}
	maps__set_unwind_libunwind_ops(maps, ops);
	maps__set_e_machine(maps, e_machine);

	return ops->prepare_access(maps);
}

void unwind__flush_access(struct maps *maps)
{
	libunwind_arch__flush_access(maps);
}

void unwind__finish_access(struct maps *maps)
{
	libunwind_arch__finish_access(maps);
}

int libunwind__get_entries(unwind_entry_cb_t cb, void *arg,
			 struct thread *thread,
			 struct perf_sample *data, int max_stack,
			 bool best_effort)
{
	const struct unwind_libunwind_ops *ops = maps__unwind_libunwind_ops(thread__maps(thread));

	if (ops)
		return ops->get_entries(cb, arg, thread, data, max_stack, best_effort);
	return 0;
}
