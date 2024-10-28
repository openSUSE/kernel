/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2019-2020  Realtek Corporation
 */
#ifndef __RTW89_UTIL_H__
#define __RTW89_UTIL_H__

#include "core.h"

#define rtw89_iterate_vifs_bh(rtwdev, iterator, data)                          \
	ieee80211_iterate_active_interfaces_atomic((rtwdev)->hw,               \
			IEEE80211_IFACE_ITER_NORMAL, iterator, data)

/* call this function with rtwdev->mutex is held */
#define rtw89_for_each_rtwvif(rtwdev, rtwvif)				       \
	list_for_each_entry(rtwvif, &(rtwdev)->rtwvifs_list, list)

/* Before adding rtwvif to list, we need to check if it already exist, beacase
 * in some case such as SER L2 happen during WoWLAN flow, calling reconfig
 * twice cause the list to be added twice.
 */
static inline bool rtw89_rtwvif_in_list(struct rtw89_dev *rtwdev,
					struct rtw89_vif *new)
{
	struct rtw89_vif *rtwvif;

	lockdep_assert_held(&rtwdev->mutex);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		if (rtwvif == new)
			return true;

	return false;
}

#endif
