#undef TRACE_SYSTEM
#define TRACE_SYSTEM hist

#if !defined(_TRACE_HIST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HIST_H

#include "latency_hist.h"
#include <linux/tracepoint.h>

#if !defined(CONFIG_PREEMPT_OFF_HIST) && !defined(CONFIG_INTERRUPT_OFF_HIST)
#define trace_preemptirqsoff_hist(a,b)
#else
TRACE_EVENT(preemptirqsoff_hist,

	TP_PROTO(int reason, int starthist),

	TP_ARGS(reason, starthist),

	TP_STRUCT__entry(
		__field(int,	reason	)
		__field(int,	starthist	)
	),

	TP_fast_assign(
		__entry->reason		= reason;
		__entry->starthist	= starthist;
	),

	TP_printk("reason=%s starthist=%s", getaction(__entry->reason),
		  __entry->starthist ? "start" : "stop")
);
#endif

#ifndef CONFIG_MISSED_TIMER_OFFSETS_HIST
#define trace_hrtimer_interrupt(a,b,c)
#else
TRACE_EVENT(hrtimer_interrupt,

	TP_PROTO(int cpu, long long offset, struct task_struct *task),

	TP_ARGS(cpu, offset, task),

	TP_STRUCT__entry(
		__array(char,		comm,	TASK_COMM_LEN)
		__field(int,		cpu	)
		__field(long long,	offset	)
	),

	TP_fast_assign(
		strncpy(__entry->comm, task != NULL ? task->comm : "", TASK_COMM_LEN);
		__entry->cpu	= cpu;
		__entry->offset	= offset;
	),

	TP_printk("cpu=%d offset=%lld thread=%s", __entry->cpu, __entry->offset, __entry->comm)
);
#endif

#endif /* _TRACE_HIST_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
