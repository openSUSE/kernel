// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP (Foundation Security Processor) interface for Hopper/Blackwell GPUs.
//!
//! Hopper/Blackwell use a simplified firmware boot sequence: FMC, then FSP, then GSP.
//! Unlike Turing/Ampere/Ada, there is no SEC2 (Security Engine 2) usage.
//! FSP handles secure boot directly using FMC firmware and Chain of Trust.

use kernel::{
    device,
    io::poll::read_poll_timeout,
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        fsp::Fsp as FspEngine,
        Falcon, //
    },
    firmware::fsp::FspFirmware,
    gpu::Chipset,
    regs, //
};

mod hal;

/// FSP interface for Hopper/Blackwell GPUs.
///
/// An `Fsp` is produced by [`Fsp::wait_secure_boot`], which only returns once FSP secure boot
/// has completed. It owns the FSP falcon and the FMC firmware, which are used for the subsequent
/// Chain of Trust boot.
pub(crate) struct Fsp {
    #[expect(dead_code)]
    falcon: Falcon<FspEngine>,
    #[expect(dead_code)]
    fsp_fw: FspFirmware,
}

impl Fsp {
    /// Waits for FSP secure boot completion, then returns the [`Fsp`] interface.
    ///
    /// Polls the thermal scratch register until FSP signals boot completion or the timeout
    /// elapses. Returning an [`Fsp`] only on success guarantees, at the API level, that the
    /// interface is not used before secure boot has completed.
    pub(crate) fn wait_secure_boot(
        dev: &device::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        fsp_fw: FspFirmware,
    ) -> Result<Fsp> {
        /// FSP secure boot completion timeout in milliseconds.
        const FSP_SECURE_BOOT_TIMEOUT_MS: i64 = 5000;

        let hal = hal::fsp_hal(chipset).ok_or(ENOTSUPP)?;
        let falcon = Falcon::<FspEngine>::new(dev, chipset)?;

        read_poll_timeout(
            || Ok(hal.fsp_boot_status(bar)),
            |&status| status == regs::NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS,
            Delta::from_millis(10),
            Delta::from_millis(FSP_SECURE_BOOT_TIMEOUT_MS),
        )
        .inspect_err(|e| {
            dev_err!(dev, "FSP secure boot completion error: {:?}\n", e);
        })?;

        Ok(Fsp { falcon, fsp_fw })
    }
}
