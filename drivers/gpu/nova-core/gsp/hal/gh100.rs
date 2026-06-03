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
    firmware::{
        fsp::FspFirmware,
        FIRMWARE_VERSION, //
    },
    fsp::{
        FmcBootArgs,
        Fsp, //
    },
    gpu::Chipset,
    gsp::{
        boot::BootUnloadGuard,
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
    fn boot<'a>(
        &self,
        gsp: &'a Gsp,
        dev: &'a device::Device<device::Bound>,
        bar: &'a Bar0,
        chipset: Chipset,
        fb_layout: &FbLayout,
        wpr_meta: &Coherent<GspFwWprMeta>,
        _gsp_falcon: &'a Falcon<GspEngine>,
        _sec2_falcon: &'a Falcon<Sec2>,
    ) -> Result<BootUnloadGuard<'a>> {
        let fsp_fw = FspFirmware::new(dev, chipset, FIRMWARE_VERSION)?;
        let mut fsp = Fsp::wait_secure_boot(dev, bar, chipset, fsp_fw)?;

        let args = FmcBootArgs::new(
            dev,
            chipset,
            wpr_meta.dma_handle(),
            gsp.libos.dma_handle(),
            false,
        )?;

        fsp.boot_fmc(dev, bar, fb_layout, &args)?;

        Err(ENOTSUPP)
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn GspHal = &GH100;
