/*
 * Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/static_key.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_log.h>

static const char *const comments[__NFT_TRACETYPE_MAX] = {
	[NFT_TRACETYPE_POLICY]	= "policy",
	[NFT_TRACETYPE_RETURN]	= "return",
	[NFT_TRACETYPE_RULE]	= "rule",
};

static struct nf_loginfo trace_loginfo = {
	.type = NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level = LOGLEVEL_WARNING,
			.logflags = NF_LOG_DEFAULT_MASK,
	        },
	},
};

static noinline void __nft_trace_packet(struct nft_traceinfo *info,
					const struct nft_chain *chain,
					enum nft_trace_types type)
{
	if (!info->trace || !info->nf_trace)
		return;

	info->chain = chain;
	info->type = type;

	nft_trace_notify(info);
}

static inline void nft_trace_packet(struct nft_traceinfo *info,
				    const struct nft_chain *chain,
				    const struct nft_rule *rule,
				    enum nft_trace_types type)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		const struct nft_pktinfo *pkt = info->pkt;

		info->nf_trace = pkt->skb->nf_trace;
		info->rule = rule;
		__nft_trace_packet(info, chain, type);
	}
}

static inline void nft_trace_copy_nftrace(struct nft_traceinfo *info)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		const struct nft_pktinfo *pkt = info->pkt;

		if (info->trace)
			info->nf_trace = pkt->skb->nf_trace;
	}
}

static void nft_cmp_fast_eval(const struct nft_expr *expr,
			      struct nft_regs *regs)
{
	const struct nft_cmp_fast_expr *priv = nft_expr_priv(expr);
	u32 mask = nft_cmp_fast_mask(priv->len);

	if ((regs->data[priv->sreg] & mask) == priv->data)
		return;
	regs->verdict.code = NFT_BREAK;
}

static noinline void __nft_trace_verdict(struct nft_traceinfo *info,
					 const struct nft_chain *chain,
					 const struct nft_regs *regs)
{
	const struct nft_pktinfo *pkt = info->pkt;
	enum nft_trace_types type;

	switch (regs->verdict.code) {
	case NFT_CONTINUE:
	case NFT_RETURN:
		type = NFT_TRACETYPE_RETURN;
		break;
	case NF_STOLEN:
		type = NFT_TRACETYPE_RULE;
		/* can't access skb->nf_trace; use copy */
		break;
	default:
		type = NFT_TRACETYPE_RULE;
		info->nf_trace = pkt->skb->nf_trace;
		break;
	}

	__nft_trace_packet(info, chain, type);
}

static inline void nft_trace_verdict(struct nft_traceinfo *info,
				     const struct nft_chain *chain,
				     const struct nft_rule *rule,
				     const struct nft_regs *regs)
{
	if (static_branch_unlikely(&nft_trace_enabled)) {
		info->rule = rule;
		__nft_trace_verdict(info, chain, regs);
	}
}

static bool nft_payload_fast_eval(const struct nft_expr *expr,
				  struct nft_regs *regs,
				  const struct nft_pktinfo *pkt)
{
	const struct nft_payload *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];
	unsigned char *ptr;

	if (priv->base == NFT_PAYLOAD_NETWORK_HEADER)
		ptr = skb_network_header(skb);
	else {
		if (!pkt->tprot_set)
			return false;
		ptr = skb_network_header(skb) + pkt->xt.thoff;
	}

	ptr += priv->offset;

	if (unlikely(ptr + priv->len > skb_tail_pointer(skb)))
		return false;

	*dest = 0;
	if (priv->len == 2)
		*(u16 *)dest = *(u16 *)ptr;
	else if (priv->len == 4)
		*(u32 *)dest = *(u32 *)ptr;
	else
		*(u8 *)dest = *(u8 *)ptr;
	return true;
}

struct nft_jumpstack {
	const struct nft_chain	*chain;
	const struct nft_rule	*rule;
};

unsigned int
nft_do_chain(struct nft_pktinfo *pkt, void *priv)
{
	const struct nft_chain *chain = priv, *basechain = chain;
	struct nft_base_chain *base_chain;
	const struct net *net = nft_net(pkt);
	struct nft_stats __percpu *pstats;
	const struct nft_rule *rule;
	const struct nft_expr *expr, *last;
	struct nft_regs regs = {};
	unsigned int stackptr = 0;
	struct nft_jumpstack jumpstack[NFT_JUMP_STACK_SIZE];
	struct nft_stats *stats;
	unsigned int gencursor = nft_genmask_cur(net);
	struct nft_traceinfo info;

	info.trace = false;
	if (static_branch_unlikely(&nft_trace_enabled))
		nft_trace_init(&info, pkt, &regs.verdict, basechain);
do_chain:
	rule = list_entry(&chain->rules, struct nft_rule, list);
next_rule:
	regs.verdict.code = NFT_CONTINUE;
	list_for_each_entry_continue_rcu(rule, &chain->rules, list) {

		/* This rule is not active, skip. */
		if (unlikely(rule->genmask & gencursor))
			continue;

		nft_rule_for_each_expr(expr, last, rule) {
			if (expr->ops == &nft_cmp_fast_ops)
				nft_cmp_fast_eval(expr, &regs);
			else if (expr->ops != &nft_payload_fast_ops ||
				 !nft_payload_fast_eval(expr, &regs, pkt))
				expr->ops->eval(expr, &regs, pkt);

			if (regs.verdict.code != NFT_CONTINUE)
				break;
		}

		switch (regs.verdict.code) {
		case NFT_BREAK:
			regs.verdict.code = NFT_CONTINUE;
			nft_trace_copy_nftrace(&info);
			continue;
		case NFT_CONTINUE:
			nft_trace_packet(&info, chain, rule,
					 NFT_TRACETYPE_RULE);
			continue;
		}
		break;
	}

	nft_trace_verdict(&info, chain, rule, &regs);

	switch (regs.verdict.code & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_DROP:
	case NF_QUEUE:
	case NF_STOLEN:
		return regs.verdict.code;
	}

	switch (regs.verdict.code) {
	case NFT_JUMP:
		if (WARN_ON_ONCE(stackptr >= NFT_JUMP_STACK_SIZE))
			return NF_DROP;
		jumpstack[stackptr].chain = chain;
		jumpstack[stackptr].rule  = rule;
		stackptr++;
		/* fall through */
	case NFT_GOTO:
		chain = regs.verdict.chain;
		goto do_chain;
	case NFT_CONTINUE:
		/* fall through */
	case NFT_RETURN:
		break;
	default:
		WARN_ON(1);
	}

	if (stackptr > 0) {
		stackptr--;
		chain = jumpstack[stackptr].chain;
		rule  = jumpstack[stackptr].rule;
		goto next_rule;
	}

	nft_trace_packet(&info, basechain, NULL, NFT_TRACETYPE_POLICY);

	base_chain = nft_base_chain(basechain);

	rcu_read_lock_bh();
	pstats = READ_ONCE(base_chain->stats);
	if (pstats) {
		stats = this_cpu_ptr(pstats);
		u64_stats_update_begin(&stats->syncp);
		stats->pkts++;
		stats->bytes += pkt->skb->len;
		u64_stats_update_end(&stats->syncp);
	}
	rcu_read_unlock_bh();

	return nft_base_chain(basechain)->policy;
}
EXPORT_SYMBOL_GPL(nft_do_chain);

static struct nft_expr_type *nft_basic_types[] = {
	&nft_imm_type,
	&nft_cmp_type,
	&nft_lookup_type,
	&nft_bitwise_type,
	&nft_byteorder_type,
	&nft_payload_type,
	&nft_dynset_type,
	&nft_range_type,
};

int __init nf_tables_core_module_init(void)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(nft_basic_types); i++) {
		err = nft_register_expr(nft_basic_types[i]);
		if (err)
			goto err;
	}

	return 0;

err:
	while (i-- > 0)
		nft_unregister_expr(nft_basic_types[i]);
	return err;
}

void nf_tables_core_module_exit(void)
{
	int i;

	i = ARRAY_SIZE(nft_basic_types);
	while (i-- > 0)
		nft_unregister_expr(nft_basic_types[i]);
}
