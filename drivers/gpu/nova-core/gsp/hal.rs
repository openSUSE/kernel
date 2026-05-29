// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

mod gh100;
mod tu102;

use kernel::prelude::*;

use kernel::{
    device,
    dma::Coherent, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp as GspEngine,
        sec2::Sec2,
        Falcon, //
    },
    fb::FbLayout,
    firmware::gsp::GspFirmware,
    gpu::{
        Architecture,
        Chipset, //
    },
    gsp::{
        Gsp,
        GspFwWprMeta, //
    },
};

/// Trait implemented by GSP HALs.
pub(super) trait GspHal: Send {
    /// Performs the GSP boot process, loading and running the required firmwares as needed.
    #[allow(clippy::too_many_arguments)]
    fn boot(
        &self,
        gsp: &Gsp,
        dev: &device::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        fb_layout: &FbLayout,
        wpr_meta: &Coherent<GspFwWprMeta>,
        gsp_falcon: &Falcon<GspEngine>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result;

    /// Performs HAL-specific post-GSP boot tasks.
    ///
    /// This method is called by the GSP boot code after the GSP is confirmed to be running, and
    /// after the initialization commands have been pushed onto its queue.
    fn post_boot(
        &self,
        _gsp: &Gsp,
        _dev: &device::Device<device::Bound>,
        _bar: &Bar0,
        _gsp_fw: &GspFirmware,
        _gsp_falcon: &Falcon<GspEngine>,
        _sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        Ok(())
    }
}

/// Returns the GSP HAL to be used for `chipset`.
pub(super) fn gsp_hal(chipset: Chipset) -> &'static dyn GspHal {
    match chipset.arch() {
        Architecture::Turing | Architecture::Ampere | Architecture::Ada => tu102::TU102_HAL,
        Architecture::Hopper | Architecture::BlackwellGB10x | Architecture::BlackwellGB20x => {
            gh100::GH100_HAL
        }
    }
}
