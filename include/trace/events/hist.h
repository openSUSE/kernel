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
		__field(	int,	reason	)
		__field(	int,	starthist	)
	),

	TP_fast_assign(
		__entry->reason		= reason;
		__entry->starthist	= starthist;
	),

	TP_printk("reason=%s starthist=%s", getaction(__entry->reason),
		  __entry->starthist ? "start" : "stop")
);
#endif

#endif /* _TRACE_HIST_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
