// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

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
    gpu::Chipset,
    gsp::{
        hal::GspHal,
        Gsp,
        GspFwWprMeta, //
    },
};

struct Gh100;

impl GspHal for Gh100 {
    /// Boot GSP via FSP Chain of Trust (Hopper/Blackwell+ path).
    ///
    /// This path uses FSP to establish a chain of trust and boot GSP-FMC. FSP handles
    /// the GSP boot internally - no manual GSP reset/boot is needed.
    fn boot(
        &self,
        _gsp: &Gsp,
        _dev: &device::Device<device::Bound>,
        _bar: &Bar0,
        _chipset: Chipset,
        _fb_layout: &FbLayout,
        _wpr_meta: &Coherent<GspFwWprMeta>,
        _gsp_falcon: &Falcon<GspEngine>,
        _sec2_falcon: &Falcon<Sec2>,
    ) -> Result<Option<crate::gsp::UnloadBundle>> {
        Err(ENOTSUPP)
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn GspHal = &GH100;
