/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2020-2022  Realtek Corporation
 */

#ifndef __RTW89_CHAN_H__
#define __RTW89_CHAN_H__

#include "core.h"

/* The dwell time in TU before doing rtw89_chanctx_work(). */
#define RTW89_CHANCTX_TIME_MCC_PREPARE 100
#define RTW89_CHANCTX_TIME_MCC 100

/* various MCC setting time in TU */
#define RTW89_MCC_LONG_TRIGGER_TIME 300
#define RTW89_MCC_SHORT_TRIGGER_TIME 100
#define RTW89_MCC_EARLY_TX_BCN_TIME 10
#define RTW89_MCC_EARLY_RX_BCN_TIME 5
#define RTW89_MCC_MIN_RX_BCN_TIME 10
#define RTW89_MCC_DFLT_BCN_OFST_TIME 40
#define RTW89_MCC_SWITCH_CH_TIME 3

#define RTW89_MCC_PROBE_TIMEOUT 100
#define RTW89_MCC_PROBE_MAX_TRIES 3

#define RTW89_MCC_DETECT_BCN_MAX_TRIES 2

#define RTW89_MCC_MIN_GO_DURATION \
	(RTW89_MCC_EARLY_TX_BCN_TIME + RTW89_MCC_MIN_RX_BCN_TIME)

#define RTW89_MCC_MIN_STA_DURATION \
	(RTW89_MCC_EARLY_RX_BCN_TIME + RTW89_MCC_MIN_RX_BCN_TIME)

#define RTW89_MCC_MIN_RX_BCN_WITH_SWITCH_CH_TIME \
	(RTW89_MCC_MIN_RX_BCN_TIME + RTW89_MCC_SWITCH_CH_TIME)

#define RTW89_MCC_DFLT_GROUP 0
#define RTW89_MCC_NEXT_GROUP(cur) (((cur) + 1) % 4)

#define RTW89_MCC_DFLT_TX_NULL_EARLY 7
#define RTW89_MCC_DFLT_COURTESY_SLOT 3

#define RTW89_MCC_REQ_COURTESY_TIME 5
#define RTW89_MCC_REQ_COURTESY(pattern, role)			\
({								\
	const struct rtw89_mcc_pattern *p = pattern;		\
	p->tob_ ## role <= RTW89_MCC_REQ_COURTESY_TIME ||	\
	p->toa_ ## role <= RTW89_MCC_REQ_COURTESY_TIME;		\
})

#define NUM_OF_RTW89_MCC_ROLES 2

enum rtw89_mr_wtype {
	RTW89_MR_WTYPE_NONE,
	RTW89_MR_WTYPE_NONMLD,
	RTW89_MR_WTYPE_MLD1L1R,
	RTW89_MR_WTYPE_MLD2L1R,
	RTW89_MR_WTYPE_MLD2L2R,
	RTW89_MR_WTYPE_NONMLD_NONMLD,
	RTW89_MR_WTYPE_MLD1L1R_NONMLD,
	RTW89_MR_WTYPE_MLD2L1R_NONMLD,
	RTW89_MR_WTYPE_MLD2L2R_NONMLD,
	RTW89_MR_WTYPE_UNKNOWN,
};

enum rtw89_mr_wmode {
	RTW89_MR_WMODE_NONE,
	RTW89_MR_WMODE_1CLIENT,
	RTW89_MR_WMODE_1AP,
	RTW89_MR_WMODE_1AP_1CLIENT,
	RTW89_MR_WMODE_2CLIENTS,
	RTW89_MR_WMODE_2APS,
	RTW89_MR_WMODE_UNKNOWN,
};

enum rtw89_mr_ctxtype {
	RTW89_MR_CTX_NONE,
	RTW89_MR_CTX1_2GHZ,
	RTW89_MR_CTX1_5GHZ,
	RTW89_MR_CTX1_6GHZ,
	RTW89_MR_CTX2_2GHZ,
	RTW89_MR_CTX2_5GHZ,
	RTW89_MR_CTX2_6GHZ,
	RTW89_MR_CTX2_2GHZ_5GHZ,
	RTW89_MR_CTX2_2GHZ_6GHZ,
	RTW89_MR_CTX2_5GHZ_6GHZ,
	RTW89_MR_CTX_UNKNOWN,
};

struct rtw89_mr_chanctx_info {
	enum rtw89_mr_wtype wtype;
	enum rtw89_mr_wmode wmode;
	enum rtw89_mr_ctxtype ctxtype;
};

enum rtw89_chanctx_pause_reasons {
	RTW89_CHANCTX_PAUSE_REASON_HW_SCAN,
	RTW89_CHANCTX_PAUSE_REASON_ROC,
	RTW89_CHANCTX_PAUSE_REASON_GC_BCN_LOSS,
};

struct rtw89_chanctx_pause_parm {
	const struct rtw89_vif_link *trigger;
	enum rtw89_chanctx_pause_reasons rsn;
};

struct rtw89_chanctx_cb_parm {
	int (*cb)(struct rtw89_dev *rtwdev, void *data);
	void *data;
	const char *caller;
};

struct rtw89_entity_weight {
	unsigned int registered_chanctxs;
	unsigned int active_chanctxs;
	unsigned int active_roles;
};

static inline bool rtw89_get_entity_state(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy_idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return READ_ONCE(hal->entity_active[phy_idx]);
}

static inline void rtw89_set_entity_state(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy_idx,
					  bool active)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	WRITE_ONCE(hal->entity_active[phy_idx], active);
}

static inline
enum rtw89_entity_mode rtw89_get_entity_mode(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return READ_ONCE(hal->entity_mode);
}

static inline void rtw89_set_entity_mode(struct rtw89_dev *rtwdev,
					 enum rtw89_entity_mode mode)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	WRITE_ONCE(hal->entity_mode, mode);
}

void rtw89_chan_create(struct rtw89_chan *chan, u8 center_chan, u8 primary_chan,
		       enum rtw89_band band, enum rtw89_bandwidth bandwidth);
bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_chanctx_idx idx,
			      const struct rtw89_chan *new);
int rtw89_iterate_entity_chan(struct rtw89_dev *rtwdev,
			      int (*iterator)(const struct rtw89_chan *chan,
					      void *data),
			      void *data);
void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_chanctx_idx idx,
				 const struct cfg80211_chan_def *chandef);
void rtw89_config_roc_chandef(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      const struct cfg80211_chan_def *chandef);
void rtw89_entity_init(struct rtw89_dev *rtwdev);
enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev);
void rtw89_chanctx_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_queue_chanctx_work(struct rtw89_dev *rtwdev);
void rtw89_queue_chanctx_change(struct rtw89_dev *rtwdev,
				enum rtw89_chanctx_changes change);
void rtw89_query_mr_chanctx_info(struct rtw89_dev *rtwdev, u8 inst_idx,
				 struct rtw89_mr_chanctx_info *info);
void rtw89_chanctx_track(struct rtw89_dev *rtwdev);
void rtw89_chanctx_pause(struct rtw89_dev *rtwdev,
			 const struct rtw89_chanctx_pause_parm *parm);
void rtw89_chanctx_proceed(struct rtw89_dev *rtwdev,
			   const struct rtw89_chanctx_cb_parm *cb_parm);

const struct rtw89_chan *__rtw89_mgnt_chan_get(struct rtw89_dev *rtwdev,
					       const char *caller_message,
					       u8 link_index, bool nullchk);

#define rtw89_mgnt_chan_get(rtwdev, link_index) \
	__rtw89_mgnt_chan_get(rtwdev, __func__, link_index, false)

static inline const struct rtw89_chan *
rtw89_mgnt_chan_get_or_null(struct rtw89_dev *rtwdev, u8 link_index)
{
	return __rtw89_mgnt_chan_get(rtwdev, NULL, link_index, true);
}

struct rtw89_mcc_links_info {
	struct rtw89_vif_link *links[NUM_OF_RTW89_MCC_ROLES];
};

void rtw89_mcc_get_links(struct rtw89_dev *rtwdev, struct rtw89_mcc_links_info *info);
void rtw89_mcc_prepare_done_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_mcc_gc_detect_beacon_work(struct wiphy *wiphy, struct wiphy_work *work);
bool rtw89_mcc_detect_go_bcn(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link);

int rtw89_chanctx_ops_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_remove(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_change(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx,
			      u32 changed);
int rtw89_chanctx_ops_assign_vif(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct ieee80211_chanctx_conf *ctx);
void rtw89_chanctx_ops_unassign_vif(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct ieee80211_chanctx_conf *ctx);
int rtw89_chanctx_ops_reassign_vif(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   struct ieee80211_chanctx_conf *old_ctx,
				   struct ieee80211_chanctx_conf *new_ctx,
				   bool replace);

#endif
