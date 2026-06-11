// SPDX-License-Identifier: GPL-2.0
#include "aslr.h"

#include "addr_location.h"
#include "debug.h"
#include "event.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "map.h"
#include "thread.h"
#include "tool.h"
#include "session.h"
#include "data.h"
#include "dso.h"
#include "pmus.h"

#include <internal/lib.h>  /* page_size */
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include <inttypes.h>
#include <unistd.h>

/**
 * struct remap_addresses_key - Key for mapping original addresses to remapped ones.
 * @dso: Pointer to the DSO (Dynamic Shared Object) associated with the mapping.
 * @invariant: Unique offset invariant within the VMA (Virtual Memory Area).
 *             Calculated as `start - pgoff`. This value remains constant when
 *             perf's internal `maps__fixup_overlap_and_insert` splits a map into
 *             fragmented VMA pieces due to overlapping events, allowing us to
 *             resolve split maps consistently back to the original VMA.
 * @pid: Process ID associated with the mapping.
 */
struct remap_addresses_key {
	struct machine *machine;
	struct dso *dso;
	u64 invariant;
	pid_t pid;
};

struct aslr_mapping {
	struct list_head node;
	u64 orig_start;
	u64 len;
	u64 remap_start;
};

struct process_top_address {
	u64 remapped_max;
};
struct aslr_tool {
	/** @tool: The tool implemented here and a pointer to a delegate to process the data. */
	struct delegate_tool tool;
	/** @machines: The machines with the input, not remapped, virtual address layout. */
	struct machines machines;
	/** @event_copy: Buffer used to create an event to pass to the delegate. */
	char event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
	/** @remap_addresses: mapping from remap_addresses_key to remapped address. */
	struct hashmap remap_addresses;
	/** @top_addresses: mapping from process to max remapped address. */
	struct hashmap top_addresses;
};

static const pid_t kernel_pid = -1;

/* Start remapping user processes from a small non-zero offset. */
static const u64 user_space_start = 0x200000;
static const u64 kernel_space_start_64 = 0xffff800010000000ULL;
static const u64 kernel_space_start_32 = 0x80000000ULL;

static size_t remap_addresses__hash(long _key, void *ctx __maybe_unused)
{
	struct remap_addresses_key *key = (struct remap_addresses_key *)_key;
	void *dso_ptr = key->dso ? RC_CHK_ACCESS(key->dso) : NULL;

	return (size_t)key->machine ^ (size_t)dso_ptr ^ key->invariant ^ key->pid;
}

static bool remap_addresses__equal(long _key1, long _key2, void *ctx __maybe_unused)
{
	struct remap_addresses_key *key1 = (struct remap_addresses_key *)_key1;
	struct remap_addresses_key *key2 = (struct remap_addresses_key *)_key2;

	return key1->machine == key2->machine &&
	       RC_CHK_EQUAL(key1->dso, key2->dso) &&
	       key1->invariant == key2->invariant &&
	       key1->pid == key2->pid;
}

struct top_addresses_key {
	struct machine *machine;
	pid_t pid;
};

static size_t top_addresses__hash(long _key, void *ctx __maybe_unused)
{
	struct top_addresses_key *key = (struct top_addresses_key *)_key;

	return (size_t)key->machine ^ key->pid;
}

static bool top_addresses__equal(long _key1, long _key2, void *ctx __maybe_unused)
{
	struct top_addresses_key *key1 = (struct top_addresses_key *)_key1;
	struct top_addresses_key *key2 = (struct top_addresses_key *)_key2;

	return key1->machine == key2->machine && key1->pid == key2->pid;
}

static u64 round_up_to_page_size(u64 addr)
{
	return (addr + page_size - 1) & ~((u64)page_size - 1);
}

struct aslr_machine_priv {
	bool kernel_maps_loaded;
};

static int aslr_tool__preload_kernel_maps(struct machine *machine)
{
	struct aslr_machine_priv *mpriv = machine->priv;

	if (!mpriv) {
		mpriv = zalloc(sizeof(*mpriv));
		if (!mpriv)
			return -ENOMEM;
		machine->priv = mpriv;
	}

	if (!mpriv->kernel_maps_loaded) {
		struct maps *kmaps = machine__kernel_maps(machine);

		if (kmaps) {
			int err = maps__load_maps(kmaps);

			if (err < 0) {
				pr_err("ASLR: Failed to preload kernel maps for machine pid %d\n",
				       machine->pid);
				return err;
			}
		}
		mpriv->kernel_maps_loaded = true;
	}
	return 0;
}

static void aslr_tool__free_machine_priv(struct machine *machine)
{
	free(machine->priv);
	machine->priv = NULL;
}

static void aslr_tool__destroy_machines_priv(struct machines *machines)
{
	struct rb_node *nd;

	aslr_tool__free_machine_priv(&machines->host);
	for (nd = rb_first_cached(&machines->guests); nd; nd = rb_next(nd)) {
		struct machine *machine = rb_entry(nd, struct machine, rb_node);

		aslr_tool__free_machine_priv(machine);
	}
}

static u64 aslr_tool__findnew_mapping(struct aslr_tool *aslr,
				      struct machine *session_machine,
				      struct thread *aslr_thread,
				      u8 cpumode, u64 start,
				      u64 len, u64 pgoff)
{
	/* Address location for dso lookup. */
	struct addr_location al;
	/* Original ASLR address based key for the remap table. */
	struct remap_addresses_key remap_key;
	/* The address in the ASLR sanitized address space less pg_off. */
	u64 *remapped_invariant_ptr;
	/* Key for the maximum address in a process. */
	struct top_addresses_key top_addr_key;
	/* Value in top address table. */
	struct process_top_address *top = NULL;
	/* Address in ASLR sanitized address space. */
	u64 remap_addr;
	/* Potentially allocated remap table key. */
	struct remap_addresses_key *new_remap_key = NULL;
	/*
	 * Potentially allocated remap table key.
	 * TODO: Avoid allocation necessary for perf 32-bit binary support.
	 */
	u64 *new_remap_val = NULL;
	int err;

	if (!aslr_thread)
		return 0;

	/* The key to look up an incoming address to the outgoing value. */
	addr_location__init(&al);
	remap_key.machine = maps__machine(thread__maps(aslr_thread));
	remap_key.pid = (cpumode == PERF_RECORD_MISC_KERNEL ||
			 cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ?
			kernel_pid : thread__pid(aslr_thread);
	if (thread__find_map(aslr_thread, cpumode, start, &al)) {
		struct dso *dso = map__dso(al.map);
		const char *dso_name = dso ? dso__long_name(dso) : NULL;

		remap_key.dso = dso;
		if (dso && !is_anon_memory(dso_name) && !is_no_dso_memory(dso_name))
			remap_key.invariant = map__start(al.map) - map__pgoff(al.map);
		else
			remap_key.invariant = map__start(al.map);
	} else {
		remap_key.dso = NULL;
		remap_key.invariant = start;
	}

	/* The key to look up top allocated address. */
	top_addr_key.machine = remap_key.machine;
	top_addr_key.pid = remap_key.pid;

	if (hashmap__find(&aslr->remap_addresses, &remap_key, &remapped_invariant_ptr)) {
		/* Mmap already exists. */
		u64 calculated_max;

		if (al.map) {
			/*
			 * The cached value is the base of the invariant. We add the
			 * offset into the VMA (start - map__start), plus the map's
			 * pgoff, to get the precise virtual address within this chunk.
			 */
			remap_addr = *remapped_invariant_ptr + map__pgoff(al.map) +
				     (start - map__start(al.map));
		} else {
			/*
			 * For unmapped memory (e.g. kernel anonymous), the cached value
			 * was stored offset by pgoff. Adding pgoff yields the true remap_addr.
			 */
			remap_addr = *remapped_invariant_ptr + pgoff;
		}

		calculated_max = remap_addr + len;

		/* See if top mapping was expanded. */
		if (hashmap__find(&aslr->top_addresses, &top_addr_key, &top)) {
			if (calculated_max > top->remapped_max)
				top->remapped_max = calculated_max;
		}
		addr_location__exit(&al);
		return remap_addr;
	}
	/* No mmap, create an entry from the top address. */
	if (hashmap__find(&aslr->top_addresses, &top_addr_key, &top)) {
		struct addr_location prev_al;
		bool is_contiguous = false;

		/* Current max allocated mmap address within the process. */
		remap_addr = top->remapped_max;

		addr_location__init(&prev_al);
		if (thread__find_map(aslr_thread, cpumode, start - 1, &prev_al)) {
			if (map__end(prev_al.map) == start)
				is_contiguous = true;
		}
		addr_location__exit(&prev_al);

		if (is_contiguous) {
			/* Contiguous mapping, do not add 1 page gap! */
			remap_addr = round_up_to_page_size(remap_addr);
		} else {
			/* Give 1 page gap from current max page. */
			remap_addr = round_up_to_page_size(remap_addr);
			remap_addr += page_size;
		}
		if (remap_addr + len > top->remapped_max)
			top->remapped_max = remap_addr + len;
	} else {
		/* First address of the process, allocate key and first top address. */
		struct top_addresses_key *tk;
		struct process_top_address *top_val;
		struct perf_env *env = session_machine ? session_machine->env : NULL;
		bool is_64 = env ? perf_env__kernel_is_64_bit(env) : (sizeof(void *) == 8);
		u64 kernel_start_addr = is_64 ? kernel_space_start_64 : kernel_space_start_32;

		remap_addr = (cpumode == PERF_RECORD_MISC_KERNEL ||
			      cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ?
			     kernel_start_addr : user_space_start;
		remap_addr = round_up_to_page_size(remap_addr);

		tk = malloc(sizeof(*tk));
		top_val = malloc(sizeof(*top_val));
		if (!tk || !top_val) {
			err = -ENOMEM;
		} else {
			*tk = top_addr_key;
			top_val->remapped_max = remap_addr + len;
			err = hashmap__insert(&aslr->top_addresses, tk, top_val,
					      HASHMAP_ADD, NULL, NULL);
		}
		if (err) {
			errno = -err;
			pr_err("Failure to add ASLR process top address %m\n");
			free(tk);
			free(top_val);
			addr_location__exit(&al);
			return 0;
		}
	}
	/* Create rmeapping entry. */
	new_remap_key = malloc(sizeof(*new_remap_key));
	new_remap_val = malloc(sizeof(u64));
	if (!new_remap_key || !new_remap_val) {
		err = -ENOMEM;
	} else {
		*new_remap_key = remap_key;
		new_remap_key->dso = dso__get(remap_key.dso);
		if (cpumode == PERF_RECORD_MISC_KERNEL ||
		    cpumode == PERF_RECORD_MISC_GUEST_KERNEL) {
			if (al.map) {
				*new_remap_val = remap_addr -
						 (start - map__start(al.map)) -
						 map__pgoff(al.map);
			} else {
				/*
				 * Subtract pgoff from the base virtual address so that
				 * when the lookup path adds pgoff back, it perfectly
				 * cancels out and returns remap_addr.
				 */
				*new_remap_val = remap_addr - pgoff;
			}
		} else {
			*new_remap_val = remap_addr - (al.map ? (start - map__start(al.map)) +
							    map__pgoff(al.map) : pgoff);
		}
		err = hashmap__add(&aslr->remap_addresses, new_remap_key, new_remap_val);
		if (err)
			dso__put(new_remap_key->dso);
	}
	if (err) {
		errno = -err;
		pr_err("Failure to add ASLR remapping %m\n");
		free(new_remap_key);
		free(new_remap_val);
		addr_location__exit(&al);
		return 0;
	}
	addr_location__exit(&al);
	return remap_addr;
}

static int aslr_tool__process_mmap(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	u8 cpumode;
	struct thread *thread;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;
	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_mmap(tool, event, sample, aslr_machine);
	if (err)
		return err;

	thread = machine__findnew_thread(aslr_machine, event->mmap.pid, event->mmap.tid);
	if (!thread)
		return -ENOMEM;
	memcpy(&new_event->mmap, &event->mmap, event->mmap.header.size);
	/* Remaps the mmap.start. */
	new_event->mmap.start = aslr_tool__findnew_mapping(aslr, machine, thread, cpumode,
							   event->mmap.start,
							   event->mmap.len,
							   event->mmap.pgoff);
	/*
	 * For anonymous memory (and kernel maps), the kernel populates the
	 * event's pgoff field with the original un-obfuscated virtual address
	 * in bytes (i.e. (addr >> PAGE_SHIFT) << PAGE_SHIFT).
	 * We must overwrite pgoff with the new remapped byte address to prevent
	 * leaking the original ASLR layout.
	 */
	if (is_anon_memory(event->mmap.filename) || is_no_dso_memory(event->mmap.filename) ||
	    ((cpumode == PERF_RECORD_MISC_KERNEL || cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
	     !is_kernel_module(event->mmap.filename, cpumode)))
		new_event->mmap.pgoff = new_event->mmap.start;
	err = delegate->mmap(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_mmap2(const struct perf_tool *tool,
				    union perf_event *event,
				    struct perf_sample *sample,
				    struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	u8 cpumode;
	struct thread *thread;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;
	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_mmap2(tool, event, sample, aslr_machine);
	if (err)
		return err;

	thread = machine__findnew_thread(aslr_machine, event->mmap2.pid, event->mmap2.tid);
	if (!thread)
		return -ENOMEM;
	memcpy(&new_event->mmap2, &event->mmap2, event->mmap2.header.size);
	/* Remaps the mmap.start. */
	new_event->mmap2.start = aslr_tool__findnew_mapping(aslr, machine, thread, cpumode,
							    event->mmap2.start,
							    event->mmap2.len,
							    event->mmap2.pgoff);
	/*
	 * For anonymous memory (and kernel maps), the kernel populates the
	 * event's pgoff field with the original un-obfuscated virtual address
	 * in bytes (i.e. (addr >> PAGE_SHIFT) << PAGE_SHIFT).
	 * We must overwrite pgoff with the new remapped byte address to prevent
	 * leaking the original ASLR layout.
	 */
	if (is_anon_memory(event->mmap2.filename) || is_no_dso_memory(event->mmap2.filename) ||
	    ((cpumode == PERF_RECORD_MISC_KERNEL || cpumode == PERF_RECORD_MISC_GUEST_KERNEL) &&
	     !is_kernel_module(event->mmap2.filename, cpumode)))
		new_event->mmap2.pgoff = new_event->mmap2.start;
	err = delegate->mmap2(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_comm(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_comm(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->comm(delegate, event, sample, machine);
}

static int aslr_tool__process_fork(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_fork(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->fork(delegate, event, sample, machine);
}

static int aslr_tool__process_exit(const struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	struct machine *aslr_machine;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	/* Create the thread, map, etc. in the ASLR before virtual address space. */
	err = perf_event__process_exit(tool, event, sample, aslr_machine);
	if (err)
		return err;

	return delegate->exit(delegate, event, sample, machine);
}

static int aslr_tool__process_text_poke(const struct perf_tool *tool __maybe_unused,
					union perf_event *event __maybe_unused,
					struct perf_sample *sample __maybe_unused,
					struct machine *machine __maybe_unused)
{
	/* Drop in case the instruction encodes an ASLR revealing address. */
	return 0;
}

static int aslr_tool__process_ksymbol(const struct perf_tool *tool,
				      union perf_event *event,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct perf_tool *delegate;
	union perf_event *new_event;
	struct thread *thread;
	struct machine *aslr_machine;
	bool is_unregister;
	int err;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);
	delegate = aslr->tool.delegate;
	new_event = (union perf_event *)aslr->event_copy;

	aslr_machine = machines__findnew(&aslr->machines, machine->pid);
	if (!aslr_machine)
		return -ENOMEM;
	if (aslr_tool__preload_kernel_maps(aslr_machine) < 0)
		return -ENOMEM;

	thread = machine__findnew_thread(aslr_machine, kernel_pid, 0);
	if (!thread)
		return -ENOMEM;

	is_unregister = (event->ksymbol.flags & PERF_RECORD_KSYMBOL_FLAGS_UNREGISTER);

	memcpy(&new_event->ksymbol, &event->ksymbol, event->ksymbol.header.size);

	if (is_unregister) {
		new_event->ksymbol.addr = aslr_tool__findnew_mapping(aslr, machine, thread,
								     PERF_RECORD_MISC_KERNEL,
								     event->ksymbol.addr,
								     event->ksymbol.len,
								     /*pgoff=*/0);
		err = perf_event__process_ksymbol(tool, event, sample, aslr_machine);
	} else {
		err = perf_event__process_ksymbol(tool, event, sample, aslr_machine);
		new_event->ksymbol.addr = aslr_tool__findnew_mapping(aslr, machine, thread,
								     PERF_RECORD_MISC_KERNEL,
								     event->ksymbol.addr,
								     event->ksymbol.len,
								     /*pgoff=*/0);
	}
	if (err) {
		thread__put(thread);
		return err;
	}

	err = delegate->ksymbol(delegate, new_event, sample, machine);
	thread__put(thread);
	return err;
}

static int aslr_tool__process_sample(const struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine)
{
	struct delegate_tool *del_tool = container_of(tool, struct delegate_tool, tool);
	struct aslr_tool *aslr = container_of(del_tool, struct aslr_tool, tool);
	struct perf_tool *delegate = aslr->tool.delegate;

	return delegate->sample(delegate, event, sample, machine);
}

static int skipn(int fd, off_t n)
{
	char buf[4096];
	ssize_t ret;

	while (n > 0) {
		ret = read(fd, buf, min_t(off_t, n, (off_t)sizeof(buf)));
		if (ret <= 0)
			return ret;
		n -= ret;
	}

	return 0;
}

static s64 aslr_tool__process_auxtrace(const struct perf_tool *tool __maybe_unused,
				       struct perf_session *session,
				       union perf_event *event)
{
	pr_warning_once("ASLR: Dropping auxtrace data as it cannot be obfuscated.\n");
	if (perf_data__is_pipe(session->data)) {
		/* Copy behavior of the stub by reading all pipe data. */
		int err = skipn(perf_data__fd(session->data), event->auxtrace.size);

		if (err < 0)
			return err;
	}
	return event->auxtrace.size;
}

static int aslr_tool__process_auxtrace_info(const struct perf_tool *tool __maybe_unused,
					    struct perf_session *session __maybe_unused,
					    union perf_event *event __maybe_unused)
{
	return 0;
}

static int aslr_tool__process_auxtrace_error(const struct perf_tool *tool __maybe_unused,
					     struct perf_session *session __maybe_unused,
					     union perf_event *event __maybe_unused)
{
	return 0;
}


void aslr_tool__strip_attr_event(union perf_event *event, struct evlist **pevlist __maybe_unused)
{
	u32 attr_size;

	attr_size = event->attr.attr.size ?: PERF_ATTR_SIZE_VER0;

	if (attr_size >= (offsetof(struct perf_event_attr, sample_type) + sizeof(u64))) {
		event->attr.attr.sample_type &= ASLR_SUPPORTED_SAMPLE_TYPE;
	}

	if (attr_size >= (offsetof(struct perf_event_attr, type) + sizeof(u32))) {
		u32 type = event->attr.attr.type;

		if (type == PERF_TYPE_BREAKPOINT &&
		    attr_size >= (offsetof(struct perf_event_attr, bp_addr) + sizeof(u64))) {
			event->attr.attr.bp_addr = 0;
		} else if (type >= PERF_TYPE_MAX) {
			struct perf_pmu *pmu;

			pmu = perf_pmus__find_by_type(type);
			if (pmu && (!strcmp(pmu->name, "kprobe") ||
				    !strcmp(pmu->name, "uprobe"))) {
				if (attr_size >= (offsetof(struct perf_event_attr, config1) + sizeof(u64)))
					event->attr.attr.config1 = 0;
				if (attr_size >= (offsetof(struct perf_event_attr, config2) + sizeof(u64)))
					event->attr.attr.config2 = 0;
			}
		}
	}
}

void aslr_tool__strip_evlist(struct perf_tool *tool __maybe_unused,
			     struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		evsel->core.attr.sample_type &= ASLR_SUPPORTED_SAMPLE_TYPE;

		if (evsel->core.attr.type == PERF_TYPE_BREAKPOINT)
			evsel->core.attr.bp_addr = 0;
		else if (evsel->core.attr.type >= PERF_TYPE_MAX) {
			struct perf_pmu *pmu = perf_pmus__find_by_type(evsel->core.attr.type);

			if (pmu && (!strcmp(pmu->name, "kprobe") ||
				    !strcmp(pmu->name, "uprobe"))) {
				evsel->core.attr.config1 = 0;
				evsel->core.attr.config2 = 0;
			}
		}
	}
}

static void aslr_tool__init(struct aslr_tool *aslr, struct perf_tool *delegate)
{
	delegate_tool__init(&aslr->tool, delegate);
	aslr->tool.tool.ordered_events = true;

	machines__init(&aslr->machines);

	hashmap__init(&aslr->remap_addresses,
		      remap_addresses__hash, remap_addresses__equal,
		      /*ctx=*/NULL);
	hashmap__init(&aslr->top_addresses,
		      top_addresses__hash, top_addresses__equal,
		      /*ctx=*/NULL);

	aslr->tool.tool.sample	= aslr_tool__process_sample;
	/* read - reads a counter, okay to delegate. */
	aslr->tool.tool.mmap	= aslr_tool__process_mmap;
	aslr->tool.tool.mmap2	= aslr_tool__process_mmap2;
	aslr->tool.tool.comm	= aslr_tool__process_comm;
	aslr->tool.tool.fork	= aslr_tool__process_fork;
	aslr->tool.tool.exit	= aslr_tool__process_exit;
	/* namesspaces, cgroup, lost, lost_sample, aux, */
	/* itrace_start, aux_output_hw_id, context_switch, throttle, unthrottle */
	/* - no virtual addresses. */
	aslr->tool.tool.ksymbol	= aslr_tool__process_ksymbol;
	/* bpf - no virtual address. */
	aslr->tool.tool.text_poke = aslr_tool__process_text_poke;
	/*
	 * event_update, tracing_data, finished_round, build_id, id_index,
	 * auxtrace_info, auxtrace_error, time_conv, thread_map, cpu_map,
	 * stat_config, stat, feature, finished_init, bpf_metadata, compressed,
	 * auxtrace - no virtual addresses.
	 */
	aslr->tool.tool.auxtrace = aslr_tool__process_auxtrace;
	aslr->tool.tool.auxtrace_info = aslr_tool__process_auxtrace_info;
	aslr->tool.tool.auxtrace_error = aslr_tool__process_auxtrace_error;
}

struct perf_tool *aslr_tool__new(struct perf_tool *delegate)
{
	struct aslr_tool *aslr = zalloc(sizeof(*aslr));

	if (!aslr)
		return NULL;

	aslr_tool__init(aslr, delegate);
	return &aslr->tool.tool;
}

void aslr_tool__delete(struct perf_tool *tool)
{
	struct delegate_tool *del_tool;
	struct aslr_tool *aslr;
	struct hashmap_entry *cur;
	size_t bkt;
	struct rb_node *nd;

	if (!tool)
		return;

	del_tool = container_of(tool, struct delegate_tool, tool);
	aslr = container_of(del_tool, struct aslr_tool, tool);

	hashmap__for_each_entry(&aslr->remap_addresses, cur, bkt) {
		struct remap_addresses_key *key = (struct remap_addresses_key *)cur->pkey;

		if (key)
			dso__put(key->dso);
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}
	hashmap__for_each_entry(&aslr->top_addresses, cur, bkt) {
		zfree(&cur->pkey);
		zfree(&cur->pvalue);
	}

	hashmap__clear(&aslr->remap_addresses);
	hashmap__clear(&aslr->top_addresses);
	aslr_tool__destroy_machines_priv(&aslr->machines);
	machines__destroy_kernel_maps(&aslr->machines);

	while ((nd = rb_first_cached(&aslr->machines.guests)) != NULL) {
		struct machine *machine = rb_entry(nd, struct machine, rb_node);

		rb_erase_cached(nd, &aslr->machines.guests);
		machine__delete(machine);
	}

	machines__exit(&aslr->machines);
	free(aslr);
}
