// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "mld.h"
#include "iface.h"
#include "link.h"
#include "mlo.h"
#include "fw/api/mac-cfg.h"
#include "fw/api/mac.h"
#include "fw/api/rs.h"

#define IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU 512
#define IWL_NAN_RSSI_CLOSE 55
#define IWL_NAN_RSSI_MIDDLE 70

bool iwl_mld_nan_supported(struct iwl_mld *mld)
{
	return fw_has_capa(&mld->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_NAN_SYNC_SUPPORT);
}

static int iwl_mld_nan_send_config_cmd(struct iwl_mld *mld,
				       struct iwl_nan_config_cmd *cmd,
				       u8 *beacon_data, size_t beacon_data_len)
{
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(MAC_CONF_GROUP, NAN_CFG_CMD),
	};

	hcmd.len[0] = sizeof(*cmd);
	hcmd.data[0] = cmd;

	if (beacon_data_len) {
		hcmd.len[1] = beacon_data_len;
		hcmd.data[1] = beacon_data;
		hcmd.dataflags[1] = IWL_HCMD_DFL_DUP;
	}

	return iwl_mld_send_cmd(mld, &hcmd);
}

static int iwl_mld_nan_config(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      enum iwl_ctxt_action action)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_nan_config_cmd cmd = {
		.action = cpu_to_le32(action),
	};
	u8 *data __free(kfree) = NULL;

	lockdep_assert_wiphy(mld->wiphy);

	ether_addr_copy(cmd.nmi_addr, vif->addr);
	cmd.master_pref = conf->master_pref;

	memcpy(cmd.cluster_id, conf->cluster_id + 4,
	       sizeof(cmd.cluster_id));

	cmd.scan_period = conf->scan_period < 255 ? conf->scan_period : 255;
	cmd.dwell_time =
		conf->scan_dwell_time < 255 ? conf->scan_dwell_time : 255;

	if (conf->discovery_beacon_interval)
		cmd.discovery_beacon_interval =
			cpu_to_le32(conf->discovery_beacon_interval);
	else
		cmd.discovery_beacon_interval =
			cpu_to_le32(IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU);

	if (conf->enable_dw_notification)
		cmd.flags = IWL_NAN_FLAG_DW_END_NOTIF_ENABLED;

	/* 2 GHz band must be supported */
	cmd.band_config[IWL_NAN_BAND_2GHZ].rssi_close =
		abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_close);
	cmd.band_config[IWL_NAN_BAND_2GHZ].rssi_middle =
		abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_middle);
	cmd.band_config[IWL_NAN_BAND_2GHZ].dw_interval =
		conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval;

	/* 5 GHz band operation is optional. Configure its operation if
	 * supported. Note that conf->bands might be zero, so we need to check
	 * the channel pointer, not the band mask.
	 */
	if (conf->band_cfgs[NL80211_BAND_5GHZ].chan) {
		cmd.hb_channel =
			conf->band_cfgs[NL80211_BAND_5GHZ].chan->hw_value;

		cmd.band_config[IWL_NAN_BAND_5GHZ].rssi_close =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_close);
		cmd.band_config[IWL_NAN_BAND_5GHZ].rssi_middle =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_middle);
		cmd.band_config[IWL_NAN_BAND_5GHZ].dw_interval =
			conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval;
	}

	if (conf->extra_nan_attrs_len || conf->vendor_elems_len) {
		data = kmalloc(conf->extra_nan_attrs_len +
			       conf->vendor_elems_len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		cmd.nan_attr_len = cpu_to_le32(conf->extra_nan_attrs_len);
		cmd.nan_vendor_elems_len = cpu_to_le32(conf->vendor_elems_len);

		if (conf->extra_nan_attrs_len)
			memcpy(data, conf->extra_nan_attrs,
			       conf->extra_nan_attrs_len);

		if (conf->vendor_elems_len)
			memcpy(data + conf->extra_nan_attrs_len,
			       conf->vendor_elems,
			       conf->vendor_elems_len);
	}

	cmd.sta_id = mld_vif->aux_sta.sta_id;
	return iwl_mld_nan_send_config_cmd(mld, &cmd, data,
					   conf->extra_nan_attrs_len +
					   conf->vendor_elems_len);
}

int iwl_mld_start_nan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct cfg80211_nan_conf *conf)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_int_sta *aux_sta = &mld_vif->aux_sta;
	int ret;

	IWL_DEBUG_MAC80211(mld, "NAN: start: bands=0x%x\n", conf->bands);

	ret = iwl_mld_update_emlsr_block(mld, true, IWL_MLD_EMLSR_BLOCKED_NAN);
	if (ret)
		return ret;

	ret = iwl_mld_add_aux_sta(mld, aux_sta);
	if (ret)
		goto unblock_emlsr;

	ret = iwl_mld_nan_config(mld, vif, conf, FW_CTXT_ACTION_ADD);
	if (ret) {
		IWL_ERR(mld, "Failed to start NAN. ret=%d\n", ret);
		goto remove_aux;
	}
	return 0;

remove_aux:
	iwl_mld_remove_aux_sta(mld, vif);
unblock_emlsr:
	iwl_mld_update_emlsr_block(mld, false, IWL_MLD_EMLSR_BLOCKED_NAN);

	return ret;
}

int iwl_mld_nan_change_config(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      u32 changes)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	IWL_DEBUG_MAC80211(mld, "NAN: change: changes=0x%x, bands=0x%x\n",
			   changes, conf->bands);

	/* Note that we do not use 'changes' as the FW always expects the
	 * complete configuration, and mac80211 always provides the complete
	 * configuration.
	 */
	return iwl_mld_nan_config(mld, vif, conf, FW_CTXT_ACTION_MODIFY);
}

int iwl_mld_stop_nan(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_nan_config_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP, NAN_CFG_CMD),
				   &cmd);
	if (ret)
		IWL_ERR(mld, "NAN: Failed to stop NAN. ret=%d\n", ret);

	/* assume that higher layer guarantees that no additional frames are
	 * added before calling this callback
	 */
	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);
	iwl_mld_remove_aux_sta(mld, vif);

	/* cancel based on object type being NAN, as the NAN objects do
	 * not have a unique identifier associated with them
	 */
	iwl_mld_cancel_notifications_of_object(mld,
					       IWL_MLD_OBJECT_TYPE_NAN,
					       0);

	iwl_mld_update_emlsr_block(mld, false, IWL_MLD_EMLSR_BLOCKED_NAN);

	return 0;
}

void iwl_mld_handle_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt)
{
	struct iwl_nan_cluster_notif *notif = (void *)pkt->data;
	struct wireless_dev *wdev = mld->nan_device_vif ?
		ieee80211_vif_to_wdev(mld->nan_device_vif) : NULL;
	bool new_cluster = !!(notif->flags &
			      IWL_NAN_CLUSTER_NOTIF_FLAG_NEW_CLUSTER);
	u8 cluster_id[ETH_ALEN] = {
		0x50, 0x6f, 0x9a, 0x01,
		notif->cluster_id[0], notif->cluster_id[1]
	};

	IWL_DEBUG_INFO(mld,
		       "NAN: cluster event: cluster_id=%pM, flags=0x%x\n",
		       cluster_id, notif->flags);

	if (IWL_FW_CHECK(mld, !wdev, "NAN: cluster event without wdev\n"))
		return;

	if (IWL_FW_CHECK(mld, !ieee80211_vif_nan_started(mld->nan_device_vif),
			 "NAN: cluster event without NAN started\n"))
		return;

	cfg80211_nan_cluster_joined(wdev, cluster_id, new_cluster, GFP_KERNEL);
}

bool iwl_mld_cancel_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt,
				      u32 obj_id)
{
	return true;
}

bool iwl_mld_cancel_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt,
				     u32 obj_id)
{
	return true;
}

void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt)
{
	struct iwl_nan_dw_end_notif *notif = (void *)pkt->data;
	struct iwl_mld_vif *mld_vif = mld->nan_device_vif ?
		iwl_mld_vif_from_mac80211(mld->nan_device_vif) :
		NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;

	IWL_INFO(mld, "NAN: DW end: band=%u\n", notif->band);

	if (IWL_FW_CHECK(mld, !mld_vif, "NAN: DW end without mld_vif\n"))
		return;

	if (IWL_FW_CHECK(mld, !ieee80211_vif_nan_started(mld->nan_device_vif),
			 "NAN: DW end without NAN started\n"))
		return;

	if (WARN_ON(mld_vif->aux_sta.sta_id == IWL_INVALID_STA))
		return;

	IWL_DEBUG_INFO(mld, "NAN: flush queues for aux sta=%u\n",
		       mld_vif->aux_sta.sta_id);

	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);

	/* TODO: currently the notification specified the band on which the DW
	 * ended. Need to change that to the actual channel on which the next DW
	 * will be started.
	 */
	switch (notif->band) {
	case IWL_NAN_BAND_2GHZ:
		chan = ieee80211_get_channel(mld->wiphy, 2437);
		break;
	case IWL_NAN_BAND_5GHZ:
		/* TODO: use the actual channel */
		chan = ieee80211_get_channel(mld->wiphy, 5745);
		break;
	default:
		IWL_FW_CHECK(mld, false,
			     "NAN: Invalid band %u in DW end notif\n",
			     notif->band);
		return;
	}

	wdev = ieee80211_vif_to_wdev(mld->nan_device_vif);
	cfg80211_next_nan_dw_notif(wdev, chan, GFP_KERNEL);
}

static void iwl_mld_nan_fill_rates(struct iwl_link_config_cmd *cmd)
{
	u32 ofdm = 0;

	/* All OFDM rates - NAN uses OFDM only, no CCK */
	ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(9) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(18) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(36) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(48) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(54) >> IWL_FIRST_OFDM_RATE;

	cmd->ofdm_rates = cpu_to_le32(ofdm);
	cmd->short_slot = cpu_to_le32(1);
}

static void iwl_mld_nan_fill_qos(struct iwl_ac_qos *ac, __le32 *qos_flags)
{
	/* AC_BK: CWmin=15, CWmax=1023, AIFSN=7, TXOP=0 */
	ac[AC_BK].cw_min = cpu_to_le16(15);
	ac[AC_BK].cw_max = cpu_to_le16(1023);
	ac[AC_BK].aifsn = 7;
	ac[AC_BK].fifos_mask = BIT(IWL_BZ_EDCA_TX_FIFO_BK);
	ac[AC_BK].edca_txop = 0;

	/* AC_BE: CWmin=15, CWmax=1023, AIFSN=3, TXOP=0 */
	ac[AC_BE].cw_min = cpu_to_le16(15);
	ac[AC_BE].cw_max = cpu_to_le16(1023);
	ac[AC_BE].aifsn = 3;
	ac[AC_BE].fifos_mask = BIT(IWL_BZ_EDCA_TX_FIFO_BE);
	ac[AC_BE].edca_txop = 0;

	/* AC_VI: CWmin=7, CWmax=15, AIFSN=2, TXOP=3008us */
	ac[AC_VI].cw_min = cpu_to_le16(7);
	ac[AC_VI].cw_max = cpu_to_le16(15);
	ac[AC_VI].aifsn = 2;
	ac[AC_VI].fifos_mask = BIT(IWL_BZ_EDCA_TX_FIFO_VI);
	ac[AC_VI].edca_txop = cpu_to_le16(3008);

	/* AC_VO: CWmin=3, CWmax=7, AIFSN=2, TXOP=1504us */
	ac[AC_VO].cw_min = cpu_to_le16(3);
	ac[AC_VO].cw_max = cpu_to_le16(7);
	ac[AC_VO].aifsn = 2;
	ac[AC_VO].fifos_mask = BIT(IWL_BZ_EDCA_TX_FIFO_VO);
	ac[AC_VO].edca_txop = cpu_to_le16(1504);

	*qos_flags |= cpu_to_le32(MAC_QOS_FLG_UPDATE_EDCA);
}

static void
iwl_mld_nan_link_prep_cmd(struct iwl_mld *mld,
			  struct iwl_mld_nan_link *nan_link,
			  struct iwl_link_config_cmd *cmd,
			  u32 modify_flags)
{
	struct ieee80211_vif *vif = mld->nan_device_vif;
	struct iwl_mld_vif *mld_vif;

	if (WARN_ON_ONCE(!vif))
		return;

	mld_vif = iwl_mld_vif_from_mac80211(vif);

	memset(cmd, 0, sizeof(*cmd));

	if (!nan_link->chanctx) {
		cmd->phy_id = cpu_to_le32(FW_CTXT_ID_INVALID);
	} else {
		struct iwl_mld_phy *mld_phy;

		mld_phy = iwl_mld_phy_from_mac80211(nan_link->chanctx);
		cmd->phy_id = cpu_to_le32(mld_phy->fw_id);
	}

	if (modify_flags & LINK_CONTEXT_MODIFY_RATES_INFO)
		iwl_mld_nan_fill_rates(cmd);

	if (modify_flags & LINK_CONTEXT_MODIFY_QOS_PARAMS)
		iwl_mld_nan_fill_qos(cmd->ac, &cmd->qos_flags);

	cmd->link_id = cpu_to_le32(nan_link->fw_id);
	cmd->mac_id = cpu_to_le32(mld_vif->fw_id);
	cmd->active = cpu_to_le32(nan_link->active);

	ether_addr_copy(cmd->local_link_addr, vif->addr);

	cmd->modify_mask = cpu_to_le32(modify_flags);
}

static struct iwl_mld_nan_link *
iwl_mld_nan_link_add(struct iwl_mld *mld,
		     struct iwl_mld_vif *mld_vif,
		     struct ieee80211_chanctx_conf *chanctx)
{
	struct iwl_mld_nan_link *nan_link;
	struct iwl_link_config_cmd cmd;
	u8 fw_id;
	int ret;

	ret = iwl_mld_allocate_link_fw_id(mld, &fw_id, ERR_PTR(-ENODEV));
	if (ret < 0)
		return NULL;

	nan_link = &mld_vif->nan.links[fw_id];

	if (WARN_ON_ONCE(nan_link->fw_id != FW_CTXT_ID_INVALID))
		goto err;

	nan_link->fw_id = fw_id;
	nan_link->chanctx = chanctx;

	iwl_mld_nan_link_prep_cmd(mld, nan_link, &cmd,
				  LINK_CONTEXT_MODIFY_RATES_INFO |
				  LINK_CONTEXT_MODIFY_QOS_PARAMS);

	ret = iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_ADD);
	if (ret) {
		nan_link->fw_id = FW_CTXT_ID_INVALID;
		nan_link->chanctx = NULL;
		goto err;
	}

	return nan_link;
err:
	RCU_INIT_POINTER(mld->fw_id_to_bss_conf[fw_id], NULL);
	return NULL;
}

static int iwl_mld_nan_link_set_active(struct iwl_mld *mld,
				       struct iwl_mld_nan_link *nan_link,
				       bool active)
{
	struct iwl_link_config_cmd cmd;
	int ret;

	if (nan_link->active == active)
		return 0;

	nan_link->active = active;

	iwl_mld_nan_link_prep_cmd(mld, nan_link, &cmd,
				  LINK_CONTEXT_MODIFY_ACTIVE);

	ret = iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_MODIFY);
	if (ret) {
		nan_link->active = !nan_link->active;
		return ret;
	}

	if (!active)
		nan_link->chanctx = NULL;

	return 0;
}

static void iwl_mld_nan_link_remove(struct iwl_mld *mld,
				    struct iwl_mld_nan_link *nan_link)
{
	struct iwl_link_config_cmd cmd = {
		.link_id = cpu_to_le32(nan_link->fw_id),
		.phy_id = cpu_to_le32(FW_CTXT_ID_INVALID),
	};

	if (WARN_ON_ONCE(nan_link->fw_id == FW_CTXT_ID_INVALID))
		return;

	iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_REMOVE);

	RCU_INIT_POINTER(mld->fw_id_to_bss_conf[nan_link->fw_id], NULL);
	nan_link->fw_id = FW_CTXT_ID_INVALID;
	nan_link->active = false;
	nan_link->chanctx = NULL;
}

static bool iwl_mld_nan_have_links(struct iwl_mld_vif *mld_vif)
{
	struct iwl_mld_nan_link *nan_link;

	for_each_mld_nan_valid_link(mld_vif, nan_link)
		return true;

	return false;
}

static struct iwl_mld_nan_link *
iwl_mld_nan_find_link(struct iwl_mld_vif *mld_vif,
		      struct ieee80211_chanctx_conf *chanctx)
{
	struct iwl_mld_nan_link *nan_link;

	for_each_mld_nan_valid_link(mld_vif, nan_link) {
		if (nan_link->chanctx == chanctx)
			return nan_link;
	}

	return NULL;
}

void iwl_mld_nan_vif_cfg_changed(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 u64 changes)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	bool previously_empty_schedule = !iwl_mld_nan_have_links(mld_vif);
	struct ieee80211_nan_sched_cfg *sched_cfg = &vif->cfg.nan_sched;
	struct iwl_mld_nan_link *links[ARRAY_SIZE(sched_cfg->channels)] = {};
	bool link_used[ARRAY_SIZE(mld_vif->nan.links)] = {};
	struct iwl_mld_nan_link *nan_link;
	bool empty_schedule = true;
	int ret;

	if (!(changes & BSS_CHANGED_NAN_LOCAL_SCHED))
		return;

	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		if (!sched_cfg->channels[i].chanreq.oper.chan)
			continue;
		empty_schedule = false;
		break;
	}

	/* add the MAC if needed (before adding links) */
	if (!empty_schedule && previously_empty_schedule) {
		WARN_ON(mld_vif->nan.mac_added);
		ret = iwl_mld_add_nan_vif(mld, vif);

		if (ret) {
			IWL_ERR(mld, "NAN: failed to add MAC (%d)\n", ret);
			return;
		}
	}

	if (!mld_vif->nan.mac_added) {
		/* nothing to do */
		return;
	}

	/* find links we can keep (same chanctx/PHY) */
	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		struct ieee80211_chanctx_conf *chanctx;
		struct iwl_mld_nan_link *link;

		chanctx = sched_cfg->channels[i].chanctx_conf;
		/* ULW */
		if (!chanctx)
			continue;

		link = iwl_mld_nan_find_link(mld_vif, chanctx);
		links[i] = link;
		if (link)
			link_used[link->fw_id] = true;
	}

	/* add/reassign links for new channels */
	for (int i = 0; i < ARRAY_SIZE(sched_cfg->channels); i++) {
		struct ieee80211_chanctx_conf *chanctx;

		/* already have an existing active link */
		if (links[i])
			continue;

		chanctx = sched_cfg->channels[i].chanctx_conf;
		/* ULW or unused slot */
		if (!chanctx)
			continue;

		/*
		 * if this fails we still update the schedule, but
		 * without a valid link we'll always ULW it
		 */
		links[i] = iwl_mld_nan_link_add(mld, mld_vif, chanctx);

		/* we have a link, activate it */
		if (links[i]) {
			link_used[links[i]->fw_id] = true;
			iwl_mld_nan_link_set_active(mld, links[i], true);
		}
	}

	/* delete unused links */
	for_each_mld_nan_valid_link(mld_vif, nan_link) {
		if (!link_used[nan_link->fw_id]) {
			iwl_mld_nan_link_set_active(mld, nan_link, false);
			iwl_mld_nan_link_remove(mld, nan_link);
		}
	}

	/* remove MAC if needed */
	if (!previously_empty_schedule && empty_schedule) {
		/* must have been added */
		WARN_ON_ONCE(!mld_vif->nan.mac_added);

		/* mac80211 should reconfigure same state */
		if (!WARN_ON_ONCE(mld->fw_status.in_hw_restart))
			iwl_mld_rm_vif(mld, vif);
	}
}
