// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for link helper functions
 *
 * Copyright (C) 2024-2026 Intel Corporation
 */
#include <kunit/static_stub.h>

#include "utils.h"
#include "mld.h"
#include "link.h"
#include "iface.h"
#include "fw/api/mac-cfg.h"

static const struct missed_beacon_test_case {
	const char *desc;
	struct {
		struct iwl_missed_beacons_notif notif;
		bool emlsr;
	} input;
	struct {
		bool disconnected;
		bool emlsr;
	} output;
} missed_beacon_cases[] = {
	{
		.desc = "no EMLSR, no disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(4),
		},
	},
	{
		.desc = "no EMLSR, no beacon loss since Rx, no disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(20),
		},
	},
	{
		.desc = "no EMLSR, beacon loss since Rx, disconnect",
		.input.notif = {
			.consec_missed_beacons = cpu_to_le32(20),
			.consec_missed_beacons_since_last_rx =
				cpu_to_le32(10),
		},
		.output.disconnected = true,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_missed_beacon, missed_beacon_cases, desc);

static void fake_ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	vif->cfg.assoc = false;
}

static void test_missed_beacon(struct kunit *test)
{
	struct iwl_mld *mld = test->priv;
	struct iwl_missed_beacons_notif *notif;
	const struct missed_beacon_test_case *test_param =
		(const void *)(test->param_value);
	struct ieee80211_vif *vif;
	struct iwl_rx_packet *pkt;
	struct iwl_mld_kunit_link link1 = {
		.id = 0,
		.chandef = &chandef_6ghz_160mhz,
	};
	struct iwl_mld_kunit_link link2 = {
		.id = 1,
		.chandef = &chandef_5ghz_80mhz,
	};

	kunit_activate_static_stub(test, ieee80211_connection_loss,
				   fake_ieee80211_connection_loss);
	pkt = iwl_mld_kunit_create_pkt(test_param->input.notif);
	notif = (void *)pkt->data;

	if (test_param->input.emlsr) {
		vif = iwlmld_kunit_assoc_emlsr(&link1, &link2);
	} else {
		struct iwl_mld_vif *mld_vif;

		vif = iwlmld_kunit_setup_non_mlo_assoc(&link1);
		mld_vif = iwl_mld_vif_from_mac80211(vif);
		notif->link_id = cpu_to_le32(mld_vif->deflink.fw_id);
	}

	wiphy_lock(mld->wiphy);

	iwl_mld_handle_missed_beacon_notif(mld, pkt);

	wiphy_unlock(mld->wiphy);

	KUNIT_ASSERT_NE(test, vif->cfg.assoc, test_param->output.disconnected);

	/* TODO: add test cases for esr and check */
}

struct dup_beacon_test_case {
	const char *desc;
	enum nl80211_chan_width bandwidth;
	bool has_he_oper;
	bool dup_beacon_bit;
	s8 expected_adj;
};

static const struct dup_beacon_test_case dup_beacon_cases[] = {
	{
		.desc = "20 MHz - no duplication",
		.bandwidth = NL80211_CHAN_WIDTH_20,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 0,
	},
	{
		.desc = "40 MHz with duplication - 3 dB",
		.bandwidth = NL80211_CHAN_WIDTH_40,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 3,
	},
	{
		.desc = "80 MHz with duplication - 6 dB",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 6,
	},
	{
		.desc = "160 MHz with duplication - 9 dB",
		.bandwidth = NL80211_CHAN_WIDTH_160,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 9,
	},
	{
		.desc = "320 MHz with duplication - 12 dB",
		.bandwidth = NL80211_CHAN_WIDTH_320,
		.has_he_oper = true,
		.dup_beacon_bit = true,
		.expected_adj = 12,
	},
	{
		.desc = "80 MHz without dup bit - no adjustment",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = true,
		.dup_beacon_bit = false,
		.expected_adj = 0,
	},
	{
		.desc = "80 MHz without HE oper - no adjustment",
		.bandwidth = NL80211_CHAN_WIDTH_80,
		.has_he_oper = false,
		.dup_beacon_bit = true,
		.expected_adj = 0,
	},
};

KUNIT_ARRAY_PARAM_DESC(test_dup_beacon_rssi_adjust, dup_beacon_cases, desc);

static void test_dup_beacon_rssi_adjust(struct kunit *test)
{
	const struct dup_beacon_test_case *params = test->param_value;
	struct iwl_mld *mld = test->priv;
	struct ieee80211_bss_conf *link_conf;
	struct cfg80211_bss *bss;
	struct element *he_elem = NULL;
	s8 result;

	KUNIT_ALLOC_AND_ASSERT(test, link_conf);
	KUNIT_ALLOC_AND_ASSERT(test, bss);
	link_conf->bss = bss;

	link_conf->chanreq.oper.chan = &chan_6ghz;
	link_conf->chanreq.oper.width = params->bandwidth;

	if (params->has_he_oper) {
		struct ieee80211_he_6ghz_oper he_6ghz = {};

		if (params->dup_beacon_bit)
			he_6ghz.control =
				IEEE80211_HE_6GHZ_OPER_CTRL_DUP_BEACON;
		he_elem = iwlmld_kunit_create_he_6ghz_oper(he_6ghz);
	}

	rcu_assign_pointer(bss->beacon_ies,
			   iwlmld_kunit_create_bss_ies(he_elem));

	guard(wiphy)(mld->wiphy);
	result = iwl_mld_get_dup_beacon_rssi_adjust(mld, link_conf);

	KUNIT_EXPECT_EQ(test, result, params->expected_adj);
}

static struct kunit_case link_cases[] = {
	KUNIT_CASE_PARAM(test_missed_beacon, test_missed_beacon_gen_params),
	KUNIT_CASE_PARAM(test_dup_beacon_rssi_adjust,
			 test_dup_beacon_rssi_adjust_gen_params),
	{},
};

static struct kunit_suite link = {
	.name = "iwlmld-link",
	.test_cases = link_cases,
	.init = iwlmld_kunit_test_init,
};

kunit_test_suite(link);
