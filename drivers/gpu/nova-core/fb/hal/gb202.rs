// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! Blackwell GB20x framebuffer HAL.

use kernel::{
    prelude::*,
    sizes::SizeConstants, //
};

use crate::{
    driver::Bar0,
    fb::hal::FbHal, //
};

struct Gb202;

impl FbHal for Gb202 {
    fn read_sysmem_flush_page(&self, bar: &Bar0) -> u64 {
        super::ga100::read_sysmem_flush_page_ga100(bar)
    }

    fn write_sysmem_flush_page(&self, bar: &Bar0, addr: u64) -> Result {
        super::ga100::write_sysmem_flush_page_ga100(bar, addr);

        Ok(())
    }

    fn supports_display(&self, bar: &Bar0) -> bool {
        super::ga100::display_enabled_ga100(bar)
    }

    fn vidmem_size(&self, bar: &Bar0) -> u64 {
        super::ga102::vidmem_size_ga102(bar)
    }

    fn pmu_reserved_size(&self) -> u32 {
        super::gb100::pmu_reserved_size_gb100()
    }

    fn non_wpr_heap_size(&self) -> u32 {
        // Non-WPR heap for GB20x (see Open RM: kgspGetNonWprHeapSize, GB202+).
        u32::SZ_2M + u32::SZ_128K
    }

    fn frts_size(&self) -> u64 {
        super::tu102::frts_size_tu102()
    }
}

const GB202: Gb202 = Gb202;
pub(super) const GB202_HAL: &dyn FbHal = &GB202;
