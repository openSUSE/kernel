// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP is a hardware unit that runs FMC firmware.

use kernel::{
    device,
    dma::Coherent,
    firmware::Firmware,
    prelude::*, //
};

use crate::{
    firmware::elf,
    gpu::Chipset, //
};

pub(crate) struct FspFirmware {
    /// FMC firmware image data (only the "image" ELF section).
    #[expect(dead_code)]
    pub(crate) fmc_image: Coherent<[u8]>,
    /// Full FMC ELF for signature extraction.
    #[expect(dead_code)]
    pub(crate) fmc_elf: Firmware,
}

impl FspFirmware {
    pub(crate) fn new(
        dev: &device::Device<device::Bound>,
        chipset: Chipset,
        ver: &str,
    ) -> Result<Self> {
        let fw = super::request_firmware(dev, chipset, "fmc", ver)?;

        // FSP expects only the "image" section, not the entire ELF file.
        let fmc_image_data = elf::elf_section(fw.data(), "image").ok_or_else(|| {
            dev_err!(dev, "FMC ELF file missing 'image' section\n");
            EINVAL
        })?;
        let fmc_image = Coherent::from_slice(dev, fmc_image_data, GFP_KERNEL)?;

        Ok(Self {
            fmc_image,
            fmc_elf: fw,
        })
    }
}
